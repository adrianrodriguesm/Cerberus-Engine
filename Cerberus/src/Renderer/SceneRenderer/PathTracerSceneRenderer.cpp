#include "pch.h"
#include "PathTracerSceneRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RendererResources.h"
#include "Asset/AssetManager.h"
#include "Math/Matrix.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/RendererContext.h"
#include "Renderer/VulkanExtensions.h"
#include "ImGui/ImGuiUI.h"
#include "Utils/StringUtils.h"
// TODO: Remove these to (only for test)
#include "Renderer/ShaderCompiler.h"

#include <cmath>
#include <limits>

namespace cerberus
{
	// Keep the legacy raster test path isolated while the RT path is built in
	// independently testable phases.
	static constexpr bool ENABLE_RASTER_TEST_PATH = false;

	static constexpr bool ENABLE_ACCELERATION_STRUCTURES = true;

	static constexpr uint32_t RT_OUTPUT_BINDING = 16;
	static constexpr uint32_t RT_TLAS_BINDING = 1;
	static constexpr uint32_t RT_CAMERA_BINDING = 2;
	static constexpr uint32_t RT_MATERIALS_BINDING = 3;
	static constexpr uint32_t RT_INSTANCES_BINDING = 4;
	static constexpr uint32_t RT_TEXTURES_BINDING = 5;
	static constexpr uint32_t RT_ENVIRONMENT_BINDING = 6;
	static constexpr uint32_t RT_SKY_LIGHT_BINDING = 7;
	static constexpr uint32_t RT_PATH_TRACER_BINDING = 8;
	static constexpr uint32_t RT_DIRECTIONAL_LIGHTS_BINDING = 9;
	static constexpr uint32_t RT_ACCUMULATION_BINDING = 17;
	static constexpr uint32_t RT_MAX_BOUNCES = 4;

	static constexpr bool RT_RAYGEN_ONLY = false;

	/** /
	* Bind groups / DescriptorSets
	* Set 0 = Fully static data (e.g Enviroment Maps)
	* Set 1 = Per Frame data (e.g shadow map)
	* Set 2 = Per render pass data (e.g to be determine)
	* Set 3 = Per draw data (e.g. materials)
	/**/
	PathTracerSceneRenderer::PathTracerSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec)
	{
		m_Scene = scene;

		m_Specification = spec;
		m_ViewportResolution = spec.Resolution;
		if (m_ViewportResolution.x <= 0 || m_ViewportResolution.y <= 0)
			m_ViewportResolution = { 1920, 1080 };

		m_RenderScene = CreateScope<RenderScene>();

		const VkPhysicalDeviceLimits physicalDeviceLimits = RendererContext::GetCurrentDevice()->GetPhysicalDeviceLimits();
		CORE_ASSERT(
			physicalDeviceLimits.maxPerStageDescriptorSampledImages >= MaxRayTracingTextureCount &&
			physicalDeviceLimits.maxDescriptorSetSampledImages >= MaxRayTracingTextureCount,
			"The selected GPU cannot expose the fixed RT texture table to one shader stage");

		// ---------------------------------------------------------------------
		// RT PATH TRACING BOOTSTRAP
		// ---------------------------------------------------------------------
		// Goal: keep the RT integrator and its scene data independent from the old
		// raster test path while materials, environment lighting and accumulation
		// are introduced incrementally.
		//
		// Intentionally disabled while ENABLE_RASTER_TEST_PATH is false:
		// - raster test pass
		// - Ronaldo texture test
		// - camera/material UBOs for the raster path
		// - BLAS/TLAS builds
		// - mesh rendering
		// ---------------------------------------------------------------------

		auto& shaderLibrary = RendererResources::GetShaderLibrary();
		ShaderSpecification rtShaderSpec;
		rtShaderSpec.Name = "Raytrace";
		rtShaderSpec.Stages = {
			{ VK_SHADER_STAGE_RAYGEN_BIT_KHR,      "engine://Shaders/RayTracing/Raytrace_rgen.rgen" },
			{ VK_SHADER_STAGE_MISS_BIT_KHR,        "engine://Shaders/RayTracing/Raytrace_rmiss.rmiss" },
			{ VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "engine://Shaders/RayTracing/Raytrace_rchit.rchit" }
		};
		shaderLibrary.Load(rtShaderSpec, true);

		RenderQueueManager::ExecuteRenderQueue();

		// RT bind group layout reflected from Raytrace.rgen set 0.
		{
			Ref<Shader> rtShader = RendererResources::GetShaderLibrary().Get("Raytrace");
			BindGroupLayout::BindGroupLayoutInfo layoutInfoRT = ShaderReflection::ReflectLayoutInfo(rtShader, 0);
			m_RT_BindGroupLayout = ResourceManager::CreateBindGroupLayout(layoutInfoRT);

			BindGroupSpecification bindGroupRTSpec;
			bindGroupRTSpec.BindGroupLayout = m_RT_BindGroupLayout;
			bindGroupRTSpec.Lifetime = DescriptorAllocationLifetime::Transient;
			m_RT_BindGroup = ResourceManager::CreateBindGroup(bindGroupRTSpec);
		}

		// RT pipeline.
		{
			PipelineSpecification rtPipelineSpec;
			rtPipelineSpec.DebugName = "RayTrace";
			rtPipelineSpec.Shader = RendererResources::GetShaderLibrary().Get("Raytrace");
			rtPipelineSpec.CullMode = CullMode::Back;
			rtPipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			m_RT_Pipeline = ResourceManager::CreateRayTracingPipeline(rtPipelineSpec);
		}

		// Shader Binding Table. Raygen owns the iterative path loop, while the miss
		// and closest-hit shaders return environment or reconstructed surface data.
		{
			ShaderBindingTableSpecification sbtSpec;
			sbtSpec.RayGenPrograms = { { 0, {} } };
			sbtSpec.MissPrograms = { { 1, {} } };
			sbtSpec.HitPrograms = { { 2, {} } };
			sbtSpec.Pipeline = m_RT_Pipeline;
			m_ShaderBindingTable = CreateScope<ShaderBindingTable>(sbtSpec);
		}

		// Display output plus a persistent linear-radiance accumulation image.
		CreateRTImages();

		// Camera UBO for RT path.
		{
			UniformBufferSetSpecification uboSetCamSpec;
			uboSetCamSpec.Frames = ResourceManager::GetRendererConfig().FramesInFlight;
			uboSetCamSpec.Size = sizeof(CameraUB);
			uboSetCamSpec.BindingPoint = RT_CAMERA_BINDING;

			m_UniformBufferSetCamera = CreateRef<UniformBufferSet>(uboSetCamSpec);

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");

			rtBindGroup->SetUniformBuffer(m_UniformBufferSetCamera, RT_CAMERA_BINDING);
		}

		// SkyLight parameters and radiance cubemap. The black renderer resource is
		// always bound first so the descriptor is valid before a Scene supplies an
		// environment asset.
		{
			UniformBufferSetSpecification skyLightBufferSpecification;
			skyLightBufferSpecification.Frames = ResourceManager::GetRendererConfig().FramesInFlight;
			skyLightBufferSpecification.Size = sizeof(SkyLightUB);
			skyLightBufferSpecification.BindingPoint = RT_SKY_LIGHT_BINDING;

			m_RT_SkyLightBufferSet = CreateRef<UniformBufferSet>(skyLightBufferSpecification);

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			TextureCube* fallbackEnvironment = ResourceManager::GetTextureCube(RendererResources::GetBlackCubeTexture());
			CORE_ASSERT(rtBindGroup && fallbackEnvironment, "Failed to initialize RT SkyLight resources");

			rtBindGroup->SetUniformBuffer(m_RT_SkyLightBufferSet, RT_SKY_LIGHT_BINDING);
			rtBindGroup->SetImageView(fallbackEnvironment->GetImageView(), RT_ENVIRONMENT_BINDING);
		}

		// Per-dispatch path tracer state. Parameters.x is the number of samples
		// already represented by the accumulation image.
		{
			UniformBufferSetSpecification pathTracerBufferSpecification;
			pathTracerBufferSpecification.Frames = ResourceManager::GetRendererConfig().FramesInFlight;
			pathTracerBufferSpecification.Size = sizeof(PathTracerUB);
			pathTracerBufferSpecification.BindingPoint = RT_PATH_TRACER_BINDING;

			m_RT_PathTracerBufferSet = CreateRef<UniformBufferSet>(pathTracerBufferSpecification);

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
			rtBindGroup->SetUniformBuffer(m_RT_PathTracerBufferSet, RT_PATH_TRACER_BINDING);
		}

		// Directional lights are analytic delta lights. They use a compact UBO;
		// raygen evaluates them explicitly and traces one visibility ray per light.
		{
			UniformBufferSetSpecification directionalLightBufferSpecification;
			directionalLightBufferSpecification.Frames = ResourceManager::GetRendererConfig().FramesInFlight;
			directionalLightBufferSpecification.Size = sizeof(DirectionalLightUB);
			directionalLightBufferSpecification.BindingPoint = RT_DIRECTIONAL_LIGHTS_BINDING;

			m_RT_DirectionalLightBufferSet = CreateRef<UniformBufferSet>(directionalLightBufferSpecification);

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
			rtBindGroup->SetUniformBuffer(m_RT_DirectionalLightBufferSet, RT_DIRECTIONAL_LIGHTS_BINDING);
		}

		// GPU material table for RT. The per-instance buffer maps each TLAS hit to
		// the material selected by its mesh/submesh.
		{
			StorageBufferSetSpecification materialBufferSpec;
			materialBufferSpec.Frames = ResourceManager::GetRendererConfig().FramesInFlight;
			materialBufferSpec.Size = sizeof(GPUMaterialData);
			materialBufferSpec.BindingPoint = RT_MATERIALS_BINDING;
			materialBufferSpec.BufferUsage = BufferUsageType::Static;

			m_RT_MaterialBufferSet = CreateRef<StorageBufferSet>(materialBufferSpec);

			GPUMaterialData fallbackMaterial{};
			for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetRendererConfig().FramesInFlight; frameIndex++)
			{
				Ref<StorageBuffer> materialBuffer = m_RT_MaterialBufferSet->Get(frameIndex);
				materialBuffer->SetData(&fallbackMaterial, sizeof(GPUMaterialData), 0);
			}

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");

			rtBindGroup->SetStorageBuffer(m_RT_MaterialBufferSet, RT_MATERIALS_BINDING);
		}

		// Per-TLAS-instance data. gl_InstanceCustomIndexEXT indexes this table in
		// closest-hit, which resolves the material selected by the mesh/submesh.
		{
			StorageBufferSetSpecification instanceBufferSpec;
			instanceBufferSpec.Frames = ResourceManager::GetRendererConfig().FramesInFlight;
			instanceBufferSpec.Size = sizeof(RayTracingInstanceData);
			instanceBufferSpec.BindingPoint = RT_INSTANCES_BINDING;
			instanceBufferSpec.BufferUsage = BufferUsageType::Static;

			m_RT_InstanceBufferSet = CreateRef<StorageBufferSet>(instanceBufferSpec);

			RayTracingInstanceData fallbackInstance{};
			for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetRendererConfig().FramesInFlight; frameIndex++)
			{
				Ref<StorageBuffer> instanceBuffer = m_RT_InstanceBufferSet->Get(frameIndex);
				instanceBuffer->SetData(&fallbackInstance, sizeof(RayTracingInstanceData), 0);
			}

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");

			rtBindGroup->SetStorageBuffer(m_RT_InstanceBufferSet, RT_INSTANCES_BINDING);
		}

		if constexpr (ENABLE_RASTER_TEST_PATH)
		{
			// The old raster test path is intentionally disabled during the RT smoke test.
			// Re-enable this block later when the minimal RT path is stable.
			// Keep the existing RegisterMesh/UnregisterMesh/UpdateMeshTransform mechanisms;
			// they are not deleted, only not used by this isolated path.
		}


		RenderQueueManager::ExecuteRenderQueue();
	}

	PathTracerSceneRenderer::~PathTracerSceneRenderer()
	{
		if (m_RenderScene)
		{
			m_RenderScene->Shutdown();
			m_RenderScene.reset();
		}

		if (m_Framebuffer.IsValid())
			ResourceManager::DestroyFramebuffer(m_Framebuffer);

		if (m_Pipeline.IsValid())
			ResourceManager::DestroyGraphicsPipeline(m_Pipeline);

		if (m_RT_Pipeline.IsValid())
			ResourceManager::DestroyRayTracingPipeline(m_RT_Pipeline);

		if (m_RenderPass.IsValid())
			ResourceManager::DestroyRenderPass(m_RenderPass);

		if (m_BindGroup0.IsValid())
			ResourceManager::DestroyBindGroup(m_BindGroup0);

		if (m_BindGroup0Layout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_BindGroup0Layout);

		if (m_BindGroup1.IsValid())
			ResourceManager::DestroyBindGroup(m_BindGroup1);

		if (m_BindGroup1Layout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_BindGroup1Layout);

		if (m_RT_BindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_RT_BindGroup);

		if (m_RT_BindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_RT_BindGroupLayout);

		ReleaseRTImages();
	}

	void PathTracerSceneRenderer::BeginScene(const SceneRendererCamera& sceneRendererCamera)
	{
	}

	void PathTracerSceneRenderer::EndScene()
	{

	}

	void PathTracerSceneRenderer::ResetAccumulation()
	{
		m_AccumulatedFrames = 0;
	}

	bool PathTracerSceneRenderer::HasCameraChanged(const SceneRendererCamera& sceneRendererCamera) const
	{
		if (!m_HasPreviousCamera)
			return true;

		const glm::mat4 projection = sceneRendererCamera.Camera.GetProjection();
		constexpr float epsilon = 0.000001f;

		for (uint32_t column = 0; column < 4; ++column)
		{
			for (uint32_t row = 0; row < 4; ++row)
			{
				if (std::abs(sceneRendererCamera.ViewMatrix[column][row] - m_PreviousView[column][row]) > epsilon ||
					std::abs(projection[column][row] - m_PreviousProjection[column][row]) > epsilon)
				{
					return true;
				}
			}
		}

		return false;
	}

	void PathTracerSceneRenderer::CreateRTImages()
	{
		CORE_ASSERT(m_ViewportResolution.x > 0 && m_ViewportResolution.y > 0, "Invalid RT image resolution");

		ImageSpecification outputSpecification;
		outputSpecification.DebugName = "RT Output";
		outputSpecification.Width = static_cast<uint32_t>(m_ViewportResolution.x);
		outputSpecification.Height = static_cast<uint32_t>(m_ViewportResolution.y);
		outputSpecification.Format = ImageFormat::RGBA32F;
		outputSpecification.Usage = ImageUsage::Storage;
		outputSpecification.Flags = SAMPLE_COMPATIBLE;
		outputSpecification.Mips = 1;
		outputSpecification.Layers = 1;

		m_RT_OutputImage = ResourceManager::CreateImage(outputSpecification);
		Image* outputImage = ResourceManager::GetImage(m_RT_OutputImage);
		CORE_ASSERT(outputImage, "Failed to create RT output image");
		outputImage->Invalidate();
		outputImage->SetLayout(
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

		ImageViewProperties outputViewProperties;
		outputViewProperties.Image = m_RT_OutputImage;
		outputViewProperties.BaseMip = 0;
		outputViewProperties.MipCount = 1;
		outputViewProperties.BaseLayer = 0;
		outputViewProperties.LayerCount = 1;
		outputViewProperties.ViewType = ImageViewType::Image2D;
		outputViewProperties.SamplerSpec.SamplerWrap = SamplerWrap::Clamp;
		outputViewProperties.SamplerSpec.SamplerFilter = SamplerFilter::Linear;
		outputViewProperties.SamplerSpec.Anisotropy = false;
		outputViewProperties.SamplerSpec.MinLOD = 0.0f;
		outputViewProperties.SamplerSpec.MaxLOD = 0.0f;
		outputViewProperties.SamplerSpec.ShaderResource = true;

		m_RT_OutputImageView = ResourceManager::CreateImageView(outputViewProperties);
		ImageView* outputImageView = ResourceManager::GetImageView(m_RT_OutputImageView);
		CORE_ASSERT(outputImageView, "Failed to create RT output image view");
		outputImageView->Invalidate();
		outputImageView->UpdateDescriptor();

		ImageSpecification accumulationSpecification = outputSpecification;
		accumulationSpecification.DebugName = "RT Accumulation";
		accumulationSpecification.Flags = 0;

		m_RT_AccumulationImage = ResourceManager::CreateImage(accumulationSpecification);
		Image* accumulationImage = ResourceManager::GetImage(m_RT_AccumulationImage);
		CORE_ASSERT(accumulationImage, "Failed to create RT accumulation image");
		accumulationImage->Invalidate();
		accumulationImage->SetLayout(
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

		ImageViewProperties accumulationViewProperties = outputViewProperties;
		accumulationViewProperties.Image = m_RT_AccumulationImage;
		accumulationViewProperties.SamplerSpec.ShaderResource = false;

		m_RT_AccumulationImageView = ResourceManager::CreateImageView(accumulationViewProperties);
		ImageView* accumulationImageView = ResourceManager::GetImageView(m_RT_AccumulationImageView);
		CORE_ASSERT(accumulationImageView, "Failed to create RT accumulation image view");
		accumulationImageView->Invalidate();
		accumulationImageView->UpdateDescriptor();

		BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
		CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
		rtBindGroup->SetImageView(m_RT_OutputImageView, RT_OUTPUT_BINDING);
		rtBindGroup->SetImageView(m_RT_AccumulationImageView, RT_ACCUMULATION_BINDING);

		ResetAccumulation();
	}

	void PathTracerSceneRenderer::ReleaseRTImages()
	{
		if (m_RT_OutputImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_OutputImageView);
		if (m_RT_AccumulationImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_AccumulationImageView);
		if (m_RT_OutputImage.IsValid())
			ResourceManager::DestroyImage(m_RT_OutputImage);
		if (m_RT_AccumulationImage.IsValid())
			ResourceManager::DestroyImage(m_RT_AccumulationImage);

		m_RT_OutputImageView = {};
		m_RT_AccumulationImageView = {};
		m_RT_OutputImage = {};
		m_RT_AccumulationImage = {};
	}



	void PathTracerSceneRenderer::UpdateRTMaterialBuffer()
	{
		CORE_ASSERT(m_RT_MaterialBufferSet, "RT material buffer set is not initialized");

		std::vector<GPUMaterialData> gpuMaterials;

		if (m_RenderScene)
		{
			const auto& materialProxies = m_RenderScene->GetMaterialProxies();
			gpuMaterials.reserve(materialProxies.size());

			for (const MaterialProxy& materialProxy : materialProxies)
			{
				gpuMaterials.push_back(materialProxy.GPUData);
			}
		}

		if (gpuMaterials.empty())
		{
			gpuMaterials.emplace_back();
		}

		const uint32_t uploadSize = static_cast<uint32_t>(gpuMaterials.size() * sizeof(GPUMaterialData));

		for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetRendererConfig().FramesInFlight; frameIndex++)
		{
			Ref<StorageBuffer> materialBuffer = m_RT_MaterialBufferSet->Get(frameIndex);
			CORE_ASSERT(materialBuffer, "Invalid RT material storage buffer");
			materialBuffer->SetData(gpuMaterials.data(), uploadSize, 0);
		}
	}


	void PathTracerSceneRenderer::UpdateRTInstanceBuffer()
	{
		CORE_ASSERT(m_RT_InstanceBufferSet, "RT instance buffer set is not initialized");
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");

		const std::vector<RayTracingInstanceData>& rtInstances = m_RenderScene->GetRayTracingInstances();
		CORE_ASSERT(!rtInstances.empty(), "A valid TLAS must have at least one RT instance descriptor");

		const uint32_t uploadSize = static_cast<uint32_t>(rtInstances.size() * sizeof(RayTracingInstanceData));

		for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetRendererConfig().FramesInFlight; frameIndex++)
		{
			Ref<StorageBuffer> instanceBuffer = m_RT_InstanceBufferSet->Get(frameIndex);
			CORE_ASSERT(instanceBuffer, "Invalid RT instance storage buffer");
			instanceBuffer->SetData(rtInstances.data(), uploadSize, 0);
		}
	}

	void PathTracerSceneRenderer::UpdateRTTextureTable()
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");

		std::vector<Handle<ImageView>> textureImageViews;
		const std::vector<Handle<Texture2D>>& textures = m_RenderScene->GetRayTracingTextures();
		CORE_ASSERT(textures.size() <= MaxRayTracingTextureCount, "RT texture table capacity exceeded");
		textureImageViews.reserve(textures.size());

		for (Handle<Texture2D> textureHandle : textures)
		{
			Texture2D* texture = ResourceManager::GetTexture2D(textureHandle);
			CORE_ASSERT(texture, "RenderScene RT texture table contains an invalid Texture2D");
			textureImageViews.push_back(texture->GetImageView());
		}

		Texture2D* fallbackTexture = ResourceManager::GetTexture2D(RendererResources::GetWhiteTexture());
		CORE_ASSERT(fallbackTexture, "Renderer white fallback texture is invalid");

		BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
		CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
		rtBindGroup->SetImageViewArray(textureImageViews, fallbackTexture->GetImageView(), RT_TEXTURES_BINDING);
	}

	void PathTracerSceneRenderer::UpdateRTEnvironment()
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");

		const SkyLightProxy& skyLight = m_RenderScene->GetSkyLight();
		const Handle<TextureCube> radianceMapHandle = skyLight.RadianceMap.IsValid()
			? skyLight.RadianceMap
			: RendererResources::GetBlackCubeTexture();

		TextureCube* radianceMap = ResourceManager::GetTextureCube(radianceMapHandle);
		BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
		CORE_ASSERT(radianceMap && rtBindGroup, "Invalid RT environment resources");

		rtBindGroup->SetImageView(radianceMap->GetImageView(), RT_ENVIRONMENT_BINDING);
	}


	void PathTracerSceneRenderer::UpdateUBOData()
	{
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

		{
			CameraUB cameraUBO;
			cameraUBO.Projection = m_SceneData.SceneRendererCamera.Camera.GetProjection();
			cameraUBO.View = m_SceneData.SceneRendererCamera.ViewMatrix;
			cameraUBO.ViewProjection = cameraUBO.Projection * cameraUBO.View;
			cameraUBO.InverseView = glm::inverse(cameraUBO.View);
			cameraUBO.InverseProjection = glm::inverse(cameraUBO.Projection);
			cameraUBO.InverseViewReverseProjection =
				cameraUBO.InverseView * glm::inverse(m_SceneData.SceneRendererCamera.Camera.GetReverseProjection());
			const glm::vec3 cameraPosition = glm::vec3(cameraUBO.InverseView[3]);

			const glm::vec3 cameraRight =
				glm::normalize(glm::vec3(cameraUBO.InverseView[0]));

			const glm::vec3 cameraUp =
				glm::normalize(glm::vec3(cameraUBO.InverseView[1]));

			const glm::vec3 cameraForward =
				glm::normalize(-glm::vec3(cameraUBO.InverseView[2]));

			// Extract approximate FOV/aspect from projection matrix.
			// abs() protects against projection conventions with negative Y.
			const float tanHalfFovY = 1.0f / std::abs(cameraUBO.Projection[1][1]);
			const float aspect = std::abs(cameraUBO.Projection[1][1] / cameraUBO.Projection[0][0]);

			cameraUBO.Position = glm::vec4(cameraPosition, 1.0f);
			cameraUBO.Forward = glm::vec4(cameraForward, 0.0f);
			cameraUBO.Right = glm::vec4(cameraRight, 0.0f);
			cameraUBO.Up = glm::vec4(cameraUp, 0.0f);
			cameraUBO.Params = glm::vec4(tanHalfFovY, aspect, 0.0f, 0.0f);

			Ref<UniformBuffer> ubo = m_UniformBufferSetCamera->Get(frameIndex);
			CORE_ASSERT(ubo, "Invalid RT camera UBO");

			ubo->SetData(&cameraUBO, sizeof(CameraUB), 0);
		}

		{
			CORE_ASSERT(m_RenderScene && m_RT_SkyLightBufferSet, "RT SkyLight data is not initialized");
			const SkyLightProxy& skyLight = m_RenderScene->GetSkyLight();

			SkyLightUB skyLightUBO;
			skyLightUBO.Parameters.x = skyLight.Intensity;
			skyLightUBO.Parameters.y = skyLight.SampleLOD;

			Ref<UniformBuffer> ubo = m_RT_SkyLightBufferSet->Get(frameIndex);
			CORE_ASSERT(ubo, "Invalid RT SkyLight UBO");
			ubo->SetData(&skyLightUBO, sizeof(SkyLightUB), 0);
		}

		{
			CORE_ASSERT(m_RT_PathTracerBufferSet, "RT path tracer data is not initialized");

			PathTracerUB pathTracerUBO;
			pathTracerUBO.Parameters.x = m_AccumulatedFrames;
			pathTracerUBO.Parameters.y = RT_MAX_BOUNCES;
			pathTracerUBO.Presentation.x = m_SceneData.SceneRendererCamera.Camera.GetExposure();

			Ref<UniformBuffer> ubo = m_RT_PathTracerBufferSet->Get(frameIndex);
			CORE_ASSERT(ubo, "Invalid RT path tracer UBO");
			ubo->SetData(&pathTracerUBO, sizeof(PathTracerUB), 0);
		}

		{
			CORE_ASSERT(m_RenderScene && m_RT_DirectionalLightBufferSet, "RT directional light data is not initialized");

			DirectionalLightUB directionalLightUBO;
			directionalLightUBO.Parameters.x = m_RenderScene->GetDirectionalLightCount();
			const auto& directionalLights = m_RenderScene->GetDirectionalLights();
			for (uint32_t lightIndex = 0; lightIndex < m_RenderScene->GetDirectionalLightCount(); ++lightIndex)
			{
				const DirectionalLightProxy& light = directionalLights[lightIndex];
				directionalLightUBO.Lights[lightIndex].DirectionAndIntensity = glm::vec4(light.Direction, light.Intensity);
				directionalLightUBO.Lights[lightIndex].Radiance = glm::vec4(light.Radiance, 0.0f);
			}

			Ref<UniformBuffer> ubo = m_RT_DirectionalLightBufferSet->Get(frameIndex);
			CORE_ASSERT(ubo, "Invalid RT directional light UBO");
			ubo->SetData(&directionalLightUBO, sizeof(DirectionalLightUB), 0);
		}

		if constexpr (ENABLE_RASTER_TEST_PATH)
		{
			BindGroup* bindGroup1 = ResourceManager::GetBindGroup(m_BindGroup1);
			bindGroup1->Update(frameIndex);

			BindGroup* bindGroup0 = ResourceManager::GetBindGroup(m_BindGroup0);
			bindGroup0->Update(frameIndex);
		}

		BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
		CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
		rtBindGroup->Update(frameIndex);
	}

	void PathTracerSceneRenderer::FlushRenderPasses()
	{
		// Descriptor pool reset belongs to Renderer::BeginFrame. Resetting here can invalidate
		// descriptor sets that were already allocated/bound earlier in the same frame.
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		UpdateUBOData();

		CommandBuffer* commandBuffer = Renderer::GetMainCommandBuffer(m_Specification.SwapChainTarget);
		CORE_ASSERT(commandBuffer, "Invalid main command buffer");

		commandBuffer->BeginRenderThread();

		if constexpr (ENABLE_RASTER_TEST_PATH)
		{
			// Legacy raster test path intentionally disabled during RT development.
		}

		// Ray tracing dispatch: iterative path integration -> storage image.
		{
			VkCommandBuffer cmd = commandBuffer->GetCommandBuffer(frameIndex);

			RayTracingPipeline* rtPipeline = ResourceManager::GetRayTracingPipeline(m_RT_Pipeline);
			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			GPUBuffer* sbtBuffer = ResourceManager::GetBuffer(m_ShaderBindingTable->GetGPUBuffer());

			CORE_ASSERT(rtPipeline, "Invalid RT pipeline");
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
			CORE_ASSERT(sbtBuffer, "Invalid SBT buffer");

			const auto& rtProps = RendererContext::GetCurrentDevice()->GetRayTracingProperties();

			auto AlignUp = [](VkDeviceSize value, VkDeviceSize alignment)
				{
					return (value + alignment - 1) & ~(alignment - 1);
				};

			const uint32_t handleSize = rtProps.shaderGroupHandleSize;
			const uint32_t handleAlignment = rtProps.shaderGroupHandleAlignment;
			const uint32_t baseAlignment = rtProps.shaderGroupBaseAlignment;

			const VkDeviceSize handleSizeAligned = AlignUp(handleSize, handleAlignment);

			VkDeviceAddress sbtBase = sbtBuffer->GetDeviceAddress();
			CORE_ASSERT(sbtBase != 0, "Invalid SBT device address");

			VkStridedDeviceAddressRegionKHR raygenRegion{};
			raygenRegion.deviceAddress = sbtBase;
			raygenRegion.stride = AlignUp(handleSizeAligned, baseAlignment);
			raygenRegion.size = raygenRegion.stride;

			VkStridedDeviceAddressRegionKHR missRegion{};
			VkStridedDeviceAddressRegionKHR hitRegion{};
			VkStridedDeviceAddressRegionKHR callableRegion{};

			if constexpr (!RT_RAYGEN_ONLY)
			{
				const VkDeviceSize raygenSize = raygenRegion.size;
				const VkDeviceSize missOffset = raygenSize;
				const VkDeviceSize missSize = AlignUp(handleSizeAligned, baseAlignment);
				const VkDeviceSize hitOffset = missOffset + missSize;
				const VkDeviceSize hitSize = AlignUp(handleSizeAligned, baseAlignment);

				missRegion.deviceAddress = sbtBase + missOffset;
				missRegion.stride = handleSizeAligned;
				missRegion.size = missSize;

				hitRegion.deviceAddress = sbtBase + hitOffset;
				hitRegion.stride = handleSizeAligned;
				hitRegion.size = hitSize;
			}

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline->GetHandle());

			VkDescriptorSet rtDescriptorSet = rtBindGroup->GetHandle(frameIndex);
			CORE_ASSERT(rtDescriptorSet != VK_NULL_HANDLE, "Invalid RT descriptor set");

			vkCmdBindDescriptorSets(
				cmd,
				VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
				rtPipeline->GetVulkanPipelineLayout(),
				0,
				1,
				&rtDescriptorSet,
				0,
				nullptr);

			// Preserve storage-image writes from the previous dispatch before this
			// dispatch reads and updates the running average. The layout intentionally
			// remains GENERAL; this is a memory dependency, not a layout transition.
			Image* accumulationImage = ResourceManager::GetImage(m_RT_AccumulationImage);
			CORE_ASSERT(accumulationImage, "Invalid RT accumulation image");

			VkImageSubresourceRange accumulationRange{};
			accumulationRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			accumulationRange.baseMipLevel = 0;
			accumulationRange.levelCount = 1;
			accumulationRange.baseArrayLayer = 0;
			accumulationRange.layerCount = 1;

			accumulationImage->InsertImageMemoryBarrier(
				cmd,
				VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
				VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
				accumulationRange);

			ext::vkCmdTraceRaysKHR(
				cmd,
				&raygenRegion,
				&missRegion,
				&hitRegion,
				&callableRegion,
				(uint32_t)m_ViewportResolution.x,
				(uint32_t)m_ViewportResolution.y,
				1);
		}

		commandBuffer->EndRenderThread();
		commandBuffer->SubmitRenderThread();

		// Keep sampleIndex + 1 representable in the shader.
		if (m_AccumulatedFrames < std::numeric_limits<uint32_t>::max() - 1u)
			++m_AccumulatedFrames;
	}

	void PathTracerSceneRenderer::SubmitMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform)
	{
	}

	void PathTracerSceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return;

		if (m_ViewportResolution.x == width && m_ViewportResolution.y == height)
			return;

		m_ViewportResolution.x = width;
		m_ViewportResolution.y = height;

		if constexpr (ENABLE_RASTER_TEST_PATH)
		{
			if (m_Framebuffer.IsValid())
			{
				Framebuffer* framebuffer = ResourceManager::GetFramebuffer(m_Framebuffer);
				framebuffer->Resize(width, height);
			}
		}

		ReleaseRTImages();
		CreateRTImages();
	}

	bool PathTracerSceneRenderer::RegisterMesh(
		RenderObjectID renderObjectID,
		const Ref<Mesh>& mesh,
		const Ref<MaterialTable>& materialTable,
		const glm::mat4& transform)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		const bool registered = m_RenderScene->RegisterMesh(renderObjectID, mesh, materialTable, transform);
		if (registered)
			ResetAccumulation();
		return registered;
	}

	void PathTracerSceneRenderer::Render(const SceneRendererCamera& sceneRendererCamera)
	{
		if (HasCameraChanged(sceneRendererCamera))
		{
			ResetAccumulation();
			m_PreviousView = sceneRendererCamera.ViewMatrix;
			m_PreviousProjection = sceneRendererCamera.Camera.GetProjection();
			m_HasPreviousCamera = true;
		}

		m_SceneData.SceneRendererCamera = sceneRendererCamera;

		if constexpr (ENABLE_ACCELERATION_STRUCTURES)
		{
			CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
			m_RenderScene->PrepareRayTracing();

			// Nothing left to trace. Do not bind/trace an invalid TLAS.
			if (!m_RenderScene->HasValidTLAS())
				return;

			UpdateRTMaterialBuffer();
			UpdateRTInstanceBuffer();
			UpdateRTTextureTable();
			UpdateRTEnvironment();

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");

			rtBindGroup->SetAccelerationStructure(
				m_RenderScene->GetTLAS(),
				RT_TLAS_BINDING
			);
		}

		FlushRenderPasses();
	}

	bool PathTracerSceneRenderer::IsRegistered(RenderObjectID renderObjectID)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		return m_RenderScene->IsRegistered(renderObjectID);
	}

	bool PathTracerSceneRenderer::UnregisterMesh(RenderObjectID renderObjectID)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		const bool unregistered = m_RenderScene->UnregisterMesh(renderObjectID);
		if (unregistered)
			ResetAccumulation();
		return unregistered;
	}

	void PathTracerSceneRenderer::UpdateMeshTransform(RenderObjectID renderObjectID, const glm::mat4& transform)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		if (!m_RenderScene->IsRegistered(renderObjectID))
			return;

		m_RenderScene->UpdateMeshTransform(renderObjectID, transform);
		ResetAccumulation();
	}

	bool PathTracerSceneRenderer::UpdateMeshMaterials(RenderObjectID renderObjectID, const Ref<MaterialTable>& materialTable)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		const bool updated = m_RenderScene->UpdateMeshMaterials(renderObjectID, materialTable);
		if (updated)
			ResetAccumulation();
		return updated;
	}

	void PathTracerSceneRenderer::OnMaterialAssetChanged(const Ref<MaterialAsset>& materialAsset)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		if (m_RenderScene->UpdateMaterialProxy(materialAsset))
			ResetAccumulation();
	}

	void PathTracerSceneRenderer::SetSkyLight(const Ref<EnvironmentMap>& environment, float intensity, uint32_t sampleLOD)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		const SkyLightProxy previousSkyLight = m_RenderScene->GetSkyLight();
		m_RenderScene->SetSkyLight(environment, intensity, sampleLOD);
		const SkyLightProxy& currentSkyLight = m_RenderScene->GetSkyLight();

		constexpr float epsilon = 0.000001f;
		const bool skyLightChanged =
			previousSkyLight.RadianceMap != currentSkyLight.RadianceMap ||
			previousSkyLight.IrradianceMap != currentSkyLight.IrradianceMap ||
			std::abs(previousSkyLight.Intensity - currentSkyLight.Intensity) > epsilon ||
			std::abs(previousSkyLight.SampleLOD - currentSkyLight.SampleLOD) > epsilon;

		if (skyLightChanged)
			ResetAccumulation();
	}

	void PathTracerSceneRenderer::SetDirectionalLights(std::span<const DirectionalLight> directionalLights)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		if (m_RenderScene->SetDirectionalLights(directionalLights))
			ResetAccumulation();
	}

	void PathTracerSceneRenderer::SetSceneContext(const Ref<Scene>& scene)
	{
		if (m_Scene == scene)
			return;

		m_Scene = scene;
		m_HasPreviousCamera = false;
		ResetAccumulation();
	}

	void PathTracerSceneRenderer::OnImGuiRender()
	{
		ImGui::Begin("Renderer");
		{
			if (ui::PropertyGridHeader("Statistics", true))
			{
				ImGui::Separator();
				ImGui::Text("Path tracer samples: %u", m_AccumulatedFrames);
				if (m_RenderScene)
				{
					const SkyLightProxy& skyLight = m_RenderScene->GetSkyLight();
					ImGui::Text("SkyLight intensity: %.3f", skyLight.Intensity);
					ImGui::Text("SkyLight radiance LOD: %.0f", skyLight.SampleLOD);
					ImGui::Text("Directional lights: %u", m_RenderScene->GetDirectionalLightCount());
					if (m_RenderScene->GetDirectionalLightCount() > 0)
						ImGui::Text("Directional light 0 intensity: %.3f", m_RenderScene->GetDirectionalLights()[0].Intensity);
				}
				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
				GPUMemoryStats memoryStats = ResourceAllocator::GetStats();
				std::string used = utils::BytesToString(memoryStats.Used);
				std::string free = utils::BytesToString(memoryStats.Free);
				ImGui::Text("Used VRAM: %s", used.c_str());
				ImGui::Text("Free VRAM: %s", free.c_str());
				ui::EndTreeNode();
			}

		}
		ImGui::End();
	}

	Handle<ImageView> PathTracerSceneRenderer::GetFinalPassImage() const
	{
		return m_RT_OutputImageView;
	}
}
