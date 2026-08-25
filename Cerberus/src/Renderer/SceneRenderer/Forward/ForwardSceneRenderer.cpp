#include "pch.h"
#include "ForwardSceneRenderer.h"

#include "ImGui/ImGuiUI.h"
#include "Renderer/RHI/Command/GraphicsCommandList.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Renderer/Core/Renderer.h"
#include "Renderer/Core/RendererResources.h"
#include "Renderer/Memory/ResourceAllocator.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Utils/StringUtils.h"

#include <algorithm>

namespace cerberus
{
	namespace
	{
		constexpr uint32_t ForwardCameraBinding = 0;
		constexpr uint32_t ForwardMaterialBinding = 1;
		constexpr uint32_t ForwardTextureBinding = 2;
		constexpr uint32_t ForwardRadianceBinding = 3;
		constexpr uint32_t ForwardIrradianceBinding = 4;
		constexpr uint32_t ForwardLightingBinding = 5;
		constexpr uint32_t ForwardShadowDataBinding = 6;
		constexpr uint32_t ForwardShadowMapBinding = 7;
		constexpr uint32_t CompositeSceneColorBinding = 0;

		BindGroupLayout::BufferBinding BufferBinding(
			const char* name,
			uint32_t binding,
			VkShaderStageFlagBits stages)
		{
			BindGroupLayout::BufferBinding result;
			result.Name = name;
			result.BindingPoint = binding;
			result.ShaderStage = stages;
			result.UsageType = BufferUsageType::Static;
			return result;
		}

		BindGroupLayout::ImageBinding ImageBinding(
			const char* name,
			uint32_t binding,
			uint32_t arraySize,
			VkShaderStageFlagBits stages)
		{
			BindGroupLayout::ImageBinding result;
			result.Name = name;
			result.BindingPoint = binding;
			result.ArraySize = arraySize;
			result.ShaderStage = stages;
			return result;
		}
	}

	ForwardSceneRenderer::ForwardSceneRenderer(
		Ref<Scene> scene,
		const SceneRendererSpecification& specification)
		: m_Specification(specification), m_Scene(std::move(scene))
	{
		m_ViewportResolution.x = specification.Resolution.x > 0
			? static_cast<uint32_t>(specification.Resolution.x)
			: 1920u;
		m_ViewportResolution.y = specification.Resolution.y > 0
			? static_cast<uint32_t>(specification.Resolution.y)
			: 1080u;
		m_RenderScene = CreateScope<RenderScene>();

		const VkPhysicalDeviceLimits limits = Renderer::GetDeviceLimits();
		constexpr uint32_t requiredForwardSampledImages = MaxMaterialTextureCount + 2u;
		CORE_ASSERT(
			limits.maxPerStageDescriptorSampledImages >= requiredForwardSampledImages &&
			limits.maxDescriptorSetSampledImages >= requiredForwardSampledImages,
			"The selected GPU cannot expose the material texture table plus the two environment maps to the Forward fragment shader");

		CreateRasterResources();
	}

	ForwardSceneRenderer::~ForwardSceneRenderer()
	{
		// Descriptor/pipeline state references RenderScene textures and material
		// buffers, so release the consumer side before the producer-side proxies.
		DestroyRasterResources();

		if (m_RenderScene)
		{
			m_RenderScene->Shutdown();
			m_RenderScene.reset();
		}
	}

	void ForwardSceneRenderer::CreateRasterResources()
	{
		auto& shaderLibrary = RendererResources::GetShaderLibrary();
		CORE_ASSERT(!shaderLibrary.Contains("ForwardPBR") && !shaderLibrary.Contains("ForwardSky")
			&& !shaderLibrary.Contains("PreDepth") && !shaderLibrary.Contains("Shadow") && !shaderLibrary.Contains("Composite"),
			"Forward renderer shaders are already owned by another renderer");

		ShaderSpecification compositeShaderSpecification;
		compositeShaderSpecification.Name = "Composite";
		compositeShaderSpecification.Stages = {
			{ VK_SHADER_STAGE_VERTEX_BIT, "engine://Shaders/Composite/Composite.vert" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "engine://Shaders/Composite/Composite.frag" }
		};
		shaderLibrary.Load(compositeShaderSpecification, true);

		ShaderSpecification shadowShaderSpecification;
		shadowShaderSpecification.Name = "Shadow";
		shadowShaderSpecification.Stages = {
			{ VK_SHADER_STAGE_VERTEX_BIT, "engine://Shaders/Shadow/Shadow.vert" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "engine://Shaders/Shadow/Shadow.frag" }
		};
		shaderLibrary.Load(shadowShaderSpecification, true);

		ShaderSpecification preDepthShaderSpecification;
		preDepthShaderSpecification.Name = "PreDepth";
		preDepthShaderSpecification.Stages = {
			{ VK_SHADER_STAGE_VERTEX_BIT, "engine://Shaders/PreDepth/PreDepth.vert" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "engine://Shaders/PreDepth/PreDepth.frag" }
		};
		shaderLibrary.Load(preDepthShaderSpecification, true);

		ShaderSpecification geometryShaderSpecification;
		geometryShaderSpecification.Name = "ForwardPBR";
		geometryShaderSpecification.Stages = {
			{ VK_SHADER_STAGE_VERTEX_BIT, "engine://Shaders/Forward/ForwardPBR.vert" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "engine://Shaders/Forward/ForwardPBR.frag" }
		};
		shaderLibrary.Load(geometryShaderSpecification, true);

		ShaderSpecification skyShaderSpecification;
		skyShaderSpecification.Name = "ForwardSky";
		skyShaderSpecification.Stages = {
			{ VK_SHADER_STAGE_VERTEX_BIT, "engine://Shaders/Forward/ForwardSky.vert" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "engine://Shaders/Forward/ForwardSky.frag" }
		};
		shaderLibrary.Load(skyShaderSpecification, true);

		RenderQueueManager::ExecuteRenderQueue();

		// Dynamic Rendering has no framebuffer/render-pass object: SceneColor and
		// SceneDepth are owned directly by the renderer through ResourceManager,
		// so a future pass (e.g. PreDepth) can reference the same SceneDepth target.
		RenderTargetProperties colorTarget;
		colorTarget.DebugName = "Forward Color";
		colorTarget.Format = ImageFormat::RGBA16F;
		colorTarget.Width = m_ViewportResolution.x;
		colorTarget.Height = m_ViewportResolution.y;
		colorTarget.Resizable = true;
		colorTarget.ShaderResource = true;
		colorTarget.ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		m_SceneColor = ResourceManager::CreateRenderTarget(colorTarget);

		RenderTargetProperties depthTarget;
		depthTarget.DebugName = "Forward Depth";
		depthTarget.Format = ImageFormat::DEPTH32F;
		depthTarget.Width = m_ViewportResolution.x;
		depthTarget.Height = m_ViewportResolution.y;
		depthTarget.Resizable = true;
		depthTarget.ShaderResource = false;
		m_SceneDepth = ResourceManager::CreateRenderTarget(depthTarget);

		RenderTargetProperties compositeTarget;
		compositeTarget.DebugName = "Forward Composite";
		compositeTarget.Format = ImageFormat::RGBA;
		compositeTarget.Width = m_ViewportResolution.x;
		compositeTarget.Height = m_ViewportResolution.y;
		compositeTarget.Resizable = true;
		compositeTarget.ShaderResource = true;
		m_CompositeOutput = ResourceManager::CreateRenderTarget(compositeTarget);

		// Four independent depth targets rather than one layered/array image:
		// Image/ImageView/ResourceManager have no 2D-array view type today, and
		// four separate RenderTargetTexture handles reach the same shader-side
		// result (a fixed-size sampler array) without adding one.
		RenderTargetProperties shadowMapTarget;
		shadowMapTarget.Format = ImageFormat::DEPTH32F;
		shadowMapTarget.Width = Renderer::GetRendererConfig().ShadowMapResolution;
		shadowMapTarget.Height = Renderer::GetRendererConfig().ShadowMapResolution;
		shadowMapTarget.Resizable = false;
		shadowMapTarget.ShaderResource = true;
		for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowCascadeCount; ++cascadeIndex)
		{
			shadowMapTarget.DebugName = "Shadow Cascade " + std::to_string(cascadeIndex);
			m_ShadowMaps[cascadeIndex] = ResourceManager::CreateRenderTarget(shadowMapTarget);
		}

		PipelineSpecification shadowPipelineSpecification;
		shadowPipelineSpecification.DebugName = "Shadow";
		shadowPipelineSpecification.Shader = shaderLibrary.Get("Shadow");
		shadowPipelineSpecification.DepthAttachmentFormat = ImageFormat::DEPTH32F;
		shadowPipelineSpecification.CullMode = CullMode::Back;
		shadowPipelineSpecification.DepthTest = true;
		shadowPipelineSpecification.DepthWrite = true;
		shadowPipelineSpecification.DepthOperator = DepthCompareOperator::Less;
		shadowPipelineSpecification.Layout = {
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal" },
			{ ShaderDataType::Float3, "a_Tangent" },
			{ ShaderDataType::Float3, "a_Binormal" },
			{ ShaderDataType::Float2, "a_TexCoord" }
		};
		m_ShadowPipeline = ResourceManager::CreateGraphicsPipeline(shadowPipelineSpecification);

		PipelineSpecification preDepthPipelineSpecification;
		preDepthPipelineSpecification.DebugName = "PreDepth";
		preDepthPipelineSpecification.Shader = shaderLibrary.Get("PreDepth");
		preDepthPipelineSpecification.DepthAttachmentFormat = ImageFormat::DEPTH32F;
		preDepthPipelineSpecification.CullMode = CullMode::Back;
		preDepthPipelineSpecification.DepthTest = true;
		preDepthPipelineSpecification.DepthWrite = true;
		preDepthPipelineSpecification.DepthOperator = DepthCompareOperator::Less;
		// Layout must match the shared interleaved vertex buffer Forward also
		// binds, even though PreDepth.vert only reads a_Position.
		preDepthPipelineSpecification.Layout = {
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal" },
			{ ShaderDataType::Float3, "a_Tangent" },
			{ ShaderDataType::Float3, "a_Binormal" },
			{ ShaderDataType::Float2, "a_TexCoord" }
		};
		m_PreDepthPipeline = ResourceManager::CreateGraphicsPipeline(preDepthPipelineSpecification);

		PipelineSpecification skyPipelineSpecification;
		skyPipelineSpecification.DebugName = "Forward Sky";
		skyPipelineSpecification.Shader = shaderLibrary.Get("ForwardSky");
		skyPipelineSpecification.ColorAttachments = { { ImageFormat::RGBA16F, BlendMode::OneZero, false } };
		// Dynamic Rendering requires every pipeline drawn inside a rendering scope
		// to declare the same depth format as the scope's bound depth attachment
		// (VUID-vkCmdDraw-pDepthAttachment-06181), even when that pipeline itself
		// has depth test/write disabled and never touches the depth buffer.
		skyPipelineSpecification.DepthAttachmentFormat = ImageFormat::DEPTH32F;
		skyPipelineSpecification.CullMode = CullMode::None;
		skyPipelineSpecification.DepthTest = false;
		skyPipelineSpecification.DepthWrite = false;
		m_SkyPipeline = ResourceManager::CreateGraphicsPipeline(skyPipelineSpecification);

		PipelineSpecification geometryPipelineSpecification;
		geometryPipelineSpecification.DebugName = "Forward Geometry";
		geometryPipelineSpecification.Shader = shaderLibrary.Get("ForwardPBR");
		geometryPipelineSpecification.ColorAttachments = { { ImageFormat::RGBA16F, BlendMode::OneZero, false } };
		geometryPipelineSpecification.DepthAttachmentFormat = ImageFormat::DEPTH32F;
		geometryPipelineSpecification.CullMode = CullMode::Back;
		// PreDepth already wrote exact depth for this frame; Forward re-tests
		// (LessOrEqual, robust against float non-associativity between the two
		// pipelines' vertex shaders) without writing again.
		geometryPipelineSpecification.DepthOperator = DepthCompareOperator::LessOrEqual;
		geometryPipelineSpecification.DepthTest = true;
		geometryPipelineSpecification.DepthWrite = false;
		geometryPipelineSpecification.Layout = {
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal" },
			{ ShaderDataType::Float3, "a_Tangent" },
			{ ShaderDataType::Float3, "a_Binormal" },
			{ ShaderDataType::Float2, "a_TexCoord" }
		};
		m_GeometryPipeline = ResourceManager::CreateGraphicsPipeline(geometryPipelineSpecification);

		PipelineSpecification compositePipelineSpecification;
		compositePipelineSpecification.DebugName = "Forward Composite";
		compositePipelineSpecification.Shader = shaderLibrary.Get("Composite");
		// BlendEnabled=false means the blend factors below are never applied, but
		// Pipeline.cpp's switch still requires one of the three concrete modes
		// (matches the OneZero-with-BlendEnabled=false convention Forward/Sky use).
		compositePipelineSpecification.ColorAttachments = { { ImageFormat::RGBA, BlendMode::OneZero, false } };
		compositePipelineSpecification.CullMode = CullMode::None;
		compositePipelineSpecification.DepthTest = false;
		compositePipelineSpecification.DepthWrite = false;
		m_CompositePipeline = ResourceManager::CreateGraphicsPipeline(compositePipelineSpecification);

		BindGroupLayout::BindGroupLayoutInfo compositeLayout;
		compositeLayout.Set = 0;
		compositeLayout.ImageSamplers = {
			ImageBinding("SceneColor", CompositeSceneColorBinding, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		};
		m_CompositeBindGroupLayout = ResourceManager::CreateBindGroupLayout(compositeLayout);
		m_CompositeBindGroup = ResourceManager::CreateBindGroup({ m_CompositeBindGroupLayout, DescriptorAllocationLifetime::Persistent });

		UniformBufferSetSpecification cameraBufferSpecification;
		cameraBufferSpecification.Frames = ResourceManager::GetFramesInFlight();
		cameraBufferSpecification.Size = sizeof(CameraUB);
		cameraBufferSpecification.BindingPoint = ForwardCameraBinding;
		m_CameraBufferSet = CreateRef<UniformBufferSet>(cameraBufferSpecification);

		UniformBufferSetSpecification lightingBufferSpecification;
		lightingBufferSpecification.Frames = ResourceManager::GetFramesInFlight();
		lightingBufferSpecification.Size = sizeof(ForwardLightingUB);
		lightingBufferSpecification.BindingPoint = ForwardLightingBinding;
		m_LightingBufferSet = CreateRef<UniformBufferSet>(lightingBufferSpecification);

		UniformBufferSetSpecification shadowBufferSpecification;
		shadowBufferSpecification.Frames = ResourceManager::GetFramesInFlight();
		shadowBufferSpecification.Size = sizeof(ShadowUB);
		shadowBufferSpecification.BindingPoint = ForwardShadowDataBinding;
		m_ShadowBufferSet = CreateRef<UniformBufferSet>(shadowBufferSpecification);

		StorageBufferSetSpecification materialBufferSpecification;
		materialBufferSpecification.Frames = ResourceManager::GetFramesInFlight();
		materialBufferSpecification.Size = sizeof(GPUMaterialData);
		materialBufferSpecification.BindingPoint = ForwardMaterialBinding;
		materialBufferSpecification.BufferUsage = BufferUsageType::Static;
		m_MaterialBufferSet = CreateRef<StorageBufferSet>(materialBufferSpecification);

		GPUMaterialData fallbackMaterial{};
		for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetFramesInFlight(); ++frameIndex)
			m_MaterialBufferSet->Get(frameIndex)->SetData(&fallbackMaterial, sizeof(fallbackMaterial), 0);

		BindGroupLayout::BindGroupLayoutInfo preDepthLayout;
		preDepthLayout.Set = 0;
		preDepthLayout.UniformBuffers = {
			BufferBinding("CameraData", ForwardCameraBinding, VK_SHADER_STAGE_ALL)
		};
		m_PreDepthBindGroupLayout = ResourceManager::CreateBindGroupLayout(preDepthLayout);
		m_PreDepthBindGroup = ResourceManager::CreateBindGroup({ m_PreDepthBindGroupLayout, DescriptorAllocationLifetime::Persistent });

		BindGroupLayout::BindGroupLayoutInfo skyLayout;
		skyLayout.Set = 0;
		skyLayout.UniformBuffers = {
			BufferBinding("CameraData", ForwardCameraBinding, VK_SHADER_STAGE_ALL),
			BufferBinding("ForwardLightingData", ForwardLightingBinding, VK_SHADER_STAGE_ALL)
		};
		skyLayout.ImageSamplers = {
			ImageBinding("EnvironmentRadiance", ForwardRadianceBinding, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		};
		m_SkyBindGroupLayout = ResourceManager::CreateBindGroupLayout(skyLayout);
		m_SkyBindGroup = ResourceManager::CreateBindGroup({ m_SkyBindGroupLayout, DescriptorAllocationLifetime::Persistent });

		BindGroupLayout::BindGroupLayoutInfo geometryLayout;
		geometryLayout.Set = 0;
		geometryLayout.UniformBuffers = {
			BufferBinding("CameraData", ForwardCameraBinding, VK_SHADER_STAGE_ALL),
			BufferBinding("ForwardLightingData", ForwardLightingBinding, VK_SHADER_STAGE_ALL),
			BufferBinding("ShadowData", ForwardShadowDataBinding, VK_SHADER_STAGE_ALL)
		};
		geometryLayout.StorageBuffers = {
			BufferBinding("MaterialTable", ForwardMaterialBinding, VK_SHADER_STAGE_ALL)
		};
		geometryLayout.ImageSamplers = {
			ImageBinding("MaterialTextures", ForwardTextureBinding, MaxMaterialTextureCount, VK_SHADER_STAGE_FRAGMENT_BIT),
			ImageBinding("EnvironmentRadiance", ForwardRadianceBinding, 1, VK_SHADER_STAGE_FRAGMENT_BIT),
			ImageBinding("EnvironmentIrradiance", ForwardIrradianceBinding, 1, VK_SHADER_STAGE_FRAGMENT_BIT),
			ImageBinding("ShadowMaps", ForwardShadowMapBinding, ShadowCascadeCount, VK_SHADER_STAGE_FRAGMENT_BIT)
		};
		m_GeometryBindGroupLayout = ResourceManager::CreateBindGroupLayout(geometryLayout);
		m_GeometryBindGroup = ResourceManager::CreateBindGroup({ m_GeometryBindGroupLayout, DescriptorAllocationLifetime::Persistent });

		BindGroup* preDepthBindGroup = ResourceManager::GetBindGroup(m_PreDepthBindGroup);
		BindGroup* skyBindGroup = ResourceManager::GetBindGroup(m_SkyBindGroup);
		BindGroup* geometryBindGroup = ResourceManager::GetBindGroup(m_GeometryBindGroup);
		CORE_ASSERT(preDepthBindGroup && skyBindGroup && geometryBindGroup, "Failed to create Forward bind groups");
		preDepthBindGroup->SetUniformBuffer(m_CameraBufferSet, ForwardCameraBinding);
		skyBindGroup->SetUniformBuffer(m_CameraBufferSet, ForwardCameraBinding);
		skyBindGroup->SetUniformBuffer(m_LightingBufferSet, ForwardLightingBinding);
		geometryBindGroup->SetUniformBuffer(m_CameraBufferSet, ForwardCameraBinding);
		geometryBindGroup->SetUniformBuffer(m_LightingBufferSet, ForwardLightingBinding);
		geometryBindGroup->SetUniformBuffer(m_ShadowBufferSet, ForwardShadowDataBinding);
		geometryBindGroup->SetStorageBuffer(m_MaterialBufferSet, ForwardMaterialBinding);

		// Shadow map handles are fixed for the renderer's lifetime (unlike
		// material textures), so the binding is set once here rather than
		// refreshed every frame.
		std::vector<Handle<ImageView>> shadowMapViews;
		shadowMapViews.reserve(ShadowCascadeCount);
		for (Handle<RenderTargetTexture> shadowMap : m_ShadowMaps)
		{
			RenderTargetTexture* shadowMapTexture = ResourceManager::GetRenderTarget(shadowMap);
			CORE_ASSERT(shadowMapTexture, "Invalid shadow map render target");
			shadowMapViews.push_back(shadowMapTexture->GetImageView());
		}
		geometryBindGroup->SetImageViewArray(
			shadowMapViews,
			shadowMapViews.front(),
			ForwardShadowMapBinding);

		UpdateTextureTable();
		UpdateEnvironmentBindings();
	}

	void ForwardSceneRenderer::DestroyRasterResources()
	{
		if (m_GeometryBindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_GeometryBindGroup);
		if (m_SkyBindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_SkyBindGroup);
		if (m_PreDepthBindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_PreDepthBindGroup);
		if (m_CompositeBindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_CompositeBindGroup);
		if (m_GeometryBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_GeometryBindGroupLayout);
		if (m_SkyBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_SkyBindGroupLayout);
		if (m_PreDepthBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_PreDepthBindGroupLayout);
		if (m_CompositeBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_CompositeBindGroupLayout);

		if (m_GeometryPipeline.IsValid())
			ResourceManager::DestroyGraphicsPipeline(m_GeometryPipeline);
		if (m_SkyPipeline.IsValid())
			ResourceManager::DestroyGraphicsPipeline(m_SkyPipeline);
		if (m_PreDepthPipeline.IsValid())
			ResourceManager::DestroyGraphicsPipeline(m_PreDepthPipeline);
		if (m_ShadowPipeline.IsValid())
			ResourceManager::DestroyGraphicsPipeline(m_ShadowPipeline);
		if (m_CompositePipeline.IsValid())
			ResourceManager::DestroyGraphicsPipeline(m_CompositePipeline);
		if (m_SceneColor.IsValid())
			ResourceManager::DestroyRenderTarget(m_SceneColor);
		if (m_SceneDepth.IsValid())
			ResourceManager::DestroyRenderTarget(m_SceneDepth);
		if (m_CompositeOutput.IsValid())
			ResourceManager::DestroyRenderTarget(m_CompositeOutput);
		for (Handle<RenderTargetTexture> shadowMap : m_ShadowMaps)
		{
			if (shadowMap.IsValid())
				ResourceManager::DestroyRenderTarget(shadowMap);
		}

		m_CameraBufferSet.reset();
		m_LightingBufferSet.reset();
		m_ShadowBufferSet.reset();
		m_MaterialBufferSet.reset();

		m_GeometryBindGroup = {};
		m_SkyBindGroup = {};
		m_PreDepthBindGroup = {};
		m_CompositeBindGroup = {};
		m_GeometryBindGroupLayout = {};
		m_SkyBindGroupLayout = {};
		m_PreDepthBindGroupLayout = {};
		m_CompositeBindGroupLayout = {};
		m_GeometryPipeline = {};
		m_SkyPipeline = {};
		m_PreDepthPipeline = {};
		m_ShadowPipeline = {};
		m_CompositePipeline = {};
		m_SceneColor = {};
		m_SceneDepth = {};
		m_CompositeOutput = {};
		m_ShadowMaps.fill(Handle<RenderTargetTexture>{});

		auto& shaderLibrary = RendererResources::GetShaderLibrary();
		shaderLibrary.Unload("ForwardPBR");
		shaderLibrary.Unload("ForwardSky");
		shaderLibrary.Unload("PreDepth");
		shaderLibrary.Unload("Composite");
		shaderLibrary.Unload("Shadow");
	}

	void ForwardSceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0 || m_ViewportResolution == glm::uvec2(width, height))
			return;

		m_ViewportResolution = { width, height };

		RenderTargetTexture* sceneColor = ResourceManager::GetRenderTarget(m_SceneColor);
		CORE_ASSERT(sceneColor, "Forward SceneColor is invalid");
		sceneColor->Resize(width, height);

		RenderTargetTexture* sceneDepth = ResourceManager::GetRenderTarget(m_SceneDepth);
		CORE_ASSERT(sceneDepth, "Forward SceneDepth is invalid");
		sceneDepth->Resize(width, height);

		RenderTargetTexture* compositeOutput = ResourceManager::GetRenderTarget(m_CompositeOutput);
		CORE_ASSERT(compositeOutput, "Forward CompositeOutput is invalid");
		compositeOutput->Resize(width, height);
	}

	bool ForwardSceneRenderer::IsRegistered(RenderObjectID renderObjectID)
	{
		return m_RenderScene && m_RenderScene->IsRegistered(renderObjectID);
	}

	bool ForwardSceneRenderer::RegisterMesh(
		RenderObjectID renderObjectID,
		const Ref<Mesh>& mesh,
		const Ref<MaterialTable>& materialTable,
		const glm::mat4& transform)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		return m_RenderScene->RegisterMesh(renderObjectID, mesh, materialTable, transform);
	}

	bool ForwardSceneRenderer::UnregisterMesh(RenderObjectID renderObjectID)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		return m_RenderScene->UnregisterMesh(renderObjectID);
	}

	void ForwardSceneRenderer::UpdateMeshTransform(RenderObjectID renderObjectID, const glm::mat4& transform)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		m_RenderScene->UpdateMeshTransform(renderObjectID, transform);
	}

	bool ForwardSceneRenderer::UpdateMeshMaterials(RenderObjectID renderObjectID, const Ref<MaterialTable>& materialTable)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		return m_RenderScene->UpdateMeshMaterials(renderObjectID, materialTable);
	}

	void ForwardSceneRenderer::OnMaterialAssetChanged(const Ref<MaterialAsset>& materialAsset)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		m_RenderScene->UpdateMaterialProxy(materialAsset);
	}

	void ForwardSceneRenderer::SetSkyLight(
		const Ref<EnvironmentMap>& environment,
		float intensity,
		uint32_t sampleLOD)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		m_RenderScene->SetSkyLight(environment, intensity, sampleLOD);
	}

	void ForwardSceneRenderer::SetDirectionalLights(std::span<const DirectionalLight> directionalLights)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		m_RenderScene->SetDirectionalLights(directionalLights);
	}

	void ForwardSceneRenderer::UpdateCameraBuffer()
	{
		CameraUB cameraData;
		cameraData.ViewProjection =
			m_SceneRendererCamera.Camera.GetProjection() * m_SceneRendererCamera.ViewMatrix;
		cameraData.InverseViewProjection = glm::inverse(cameraData.ViewProjection);
		cameraData.CameraPosition = glm::vec4(m_SceneRendererCamera.ViewPosition, 1.0f);
		cameraData.Presentation.x = m_SceneRendererCamera.Camera.GetExposure();

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		m_CameraBufferSet->Get(frameIndex)->SetData(&cameraData, sizeof(cameraData), 0);
	}

	void ForwardSceneRenderer::UpdateLightingBuffer()
	{
		ForwardLightingUB lightingData;
		const SkyLightProxy& skyLight = m_RenderScene->GetSkyLight();
		lightingData.SkyLightParameters = glm::vec4(skyLight.Intensity, skyLight.SampleLOD, 0.0f, 0.0f);
		lightingData.DirectionalLightParameters.x = m_RenderScene->GetDirectionalLightCount();

		const auto& directionalLights = m_RenderScene->GetDirectionalLights();
		for (uint32_t lightIndex = 0; lightIndex < m_RenderScene->GetDirectionalLightCount(); ++lightIndex)
		{
			const DirectionalLightProxy& light = directionalLights[lightIndex];
			lightingData.DirectionalLights[lightIndex].DirectionAndIntensity =
				glm::vec4(light.Direction, light.Intensity);
			lightingData.DirectionalLights[lightIndex].Radiance = glm::vec4(light.Radiance, 0.0f);
		}

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		m_LightingBufferSet->Get(frameIndex)->SetData(&lightingData, sizeof(lightingData), 0);
	}

	void ForwardSceneRenderer::UpdateShadowBuffer()
	{
		ShadowUB shadowData;
		for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowCascadeCount; ++cascadeIndex)
		{
			shadowData.CascadeViewProjection[cascadeIndex] = m_ShadowCascades[cascadeIndex].ViewProjection;
			shadowData.CascadeSplitDepths[cascadeIndex] = m_ShadowCascades[cascadeIndex].SplitDepth;
		}

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		m_ShadowBufferSet->Get(frameIndex)->SetData(&shadowData, sizeof(shadowData), 0);
	}

	void ForwardSceneRenderer::UpdateMaterialBuffer()
	{
		const std::vector<MaterialProxy>& materialProxies = m_RenderScene->GetMaterialProxies();
		std::vector<GPUMaterialData> materialData;
		materialData.reserve(std::max(materialProxies.size(), size_t{ 1 }));
		for (const MaterialProxy& materialProxy : materialProxies)
			materialData.push_back(materialProxy.GPUData);
		if (materialData.empty())
			materialData.emplace_back();

		const uint32_t uploadSize = static_cast<uint32_t>(materialData.size() * sizeof(GPUMaterialData));
		for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetFramesInFlight(); ++frameIndex)
			m_MaterialBufferSet->Get(frameIndex)->SetData(materialData.data(), uploadSize, 0);
	}

	void ForwardSceneRenderer::UpdateTextureTable()
	{
		std::vector<Handle<ImageView>> imageViews;
		const std::vector<Handle<Texture2D>>& textures = m_RenderScene->GetMaterialTextures();
		CORE_ASSERT(textures.size() <= MaxMaterialTextureCount, "Forward material texture table capacity exceeded");
		imageViews.reserve(textures.size());
		for (Handle<Texture2D> textureHandle : textures)
		{
			Texture2D* texture = ResourceManager::GetTexture2D(textureHandle);
			CORE_ASSERT(texture, "Forward material texture table contains an invalid Texture2D");
			imageViews.push_back(texture->GetImageView());
		}

		Texture2D* fallbackTexture = ResourceManager::GetTexture2D(RendererResources::GetWhiteTexture());
		BindGroup* geometryBindGroup = ResourceManager::GetBindGroup(m_GeometryBindGroup);
		CORE_ASSERT(fallbackTexture && geometryBindGroup, "Invalid Forward material fallback resources");
		geometryBindGroup->SetImageViewArray(
			imageViews,
			fallbackTexture->GetImageView(),
			ForwardTextureBinding);
	}

	void ForwardSceneRenderer::UpdateEnvironmentBindings()
	{
		const SkyLightProxy& skyLight = m_RenderScene->GetSkyLight();
		const Handle<TextureCube> radianceHandle = skyLight.RadianceMap.IsValid()
			? skyLight.RadianceMap
			: RendererResources::GetBlackCubeTexture();
		const Handle<TextureCube> irradianceHandle = skyLight.IrradianceMap.IsValid()
			? skyLight.IrradianceMap
			: RendererResources::GetBlackCubeTexture();

		TextureCube* radiance = ResourceManager::GetTextureCube(radianceHandle);
		TextureCube* irradiance = ResourceManager::GetTextureCube(irradianceHandle);
		BindGroup* skyBindGroup = ResourceManager::GetBindGroup(m_SkyBindGroup);
		BindGroup* geometryBindGroup = ResourceManager::GetBindGroup(m_GeometryBindGroup);
		CORE_ASSERT(radiance && irradiance && skyBindGroup && geometryBindGroup,
			"Invalid Forward environment resources");

		skyBindGroup->SetImageView(radiance->GetImageView(), ForwardRadianceBinding);
		geometryBindGroup->SetImageView(radiance->GetImageView(), ForwardRadianceBinding);
		geometryBindGroup->SetImageView(irradiance->GetImageView(), ForwardIrradianceBinding);
	}

	void ForwardSceneRenderer::UpdateCompositeBindings()
	{
		// SceneColor's underlying image view is recreated on resize (RenderTargetTexture::Resize
		// -> Invalidate), so this binding is refreshed every frame rather than once at creation.
		RenderTargetTexture* sceneColor = ResourceManager::GetRenderTarget(m_SceneColor);
		BindGroup* compositeBindGroup = ResourceManager::GetBindGroup(m_CompositeBindGroup);
		CORE_ASSERT(sceneColor && compositeBindGroup, "Invalid Forward composite resources");

		compositeBindGroup->SetImageView(sceneColor->GetImageView(), CompositeSceneColorBinding);
	}

	void ForwardSceneRenderer::Render(const SceneRendererCamera& sceneRendererCamera)
	{
		m_SceneRendererCamera = sceneRendererCamera;
		UpdateCameraBuffer();
		UpdateLightingBuffer();
		UpdateMaterialBuffer();
		UpdateTextureTable();
		UpdateEnvironmentBindings();
		UpdateCompositeBindings();
		m_RenderScene->BuildDrawStream();

		// Primary-light-only shadows for now: cascades are a pure function of the
		// current camera and light direction, so they're recomputed every frame
		// rather than cached on a proxy.
		const bool hasShadowCastingLight = m_RenderScene->GetDirectionalLightCount() > 0;
		if (hasShadowCastingLight)
		{
			const glm::mat4 viewProjection = m_SceneRendererCamera.Camera.GetProjection() * m_SceneRendererCamera.ViewMatrix;
			const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
			m_ShadowCascades = ComputeShadowCascades(
				m_SceneRendererCamera.Near,
				m_SceneRendererCamera.Far,
				inverseViewProjection,
				m_RenderScene->GetDirectionalLights()[0].Direction);
			UpdateShadowBuffer();
		}

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		BindGroup* preDepthBindGroup = ResourceManager::GetBindGroup(m_PreDepthBindGroup);
		BindGroup* skyBindGroup = ResourceManager::GetBindGroup(m_SkyBindGroup);
		BindGroup* geometryBindGroup = ResourceManager::GetBindGroup(m_GeometryBindGroup);
		BindGroup* compositeBindGroup = ResourceManager::GetBindGroup(m_CompositeBindGroup);
		CORE_ASSERT(preDepthBindGroup && skyBindGroup && geometryBindGroup && compositeBindGroup, "Invalid Forward bind groups");
		preDepthBindGroup->Update(frameIndex);
		skyBindGroup->Update(frameIndex);
		geometryBindGroup->Update(frameIndex);
		compositeBindGroup->Update(frameIndex);

		m_RenderGraph.Reset();
		const RGTextureHandle sceneColorRG = m_RenderGraph.ImportRenderTarget(m_SceneColor, "SceneColor");
		const RGTextureHandle sceneDepthRG = m_RenderGraph.ImportRenderTarget(m_SceneDepth, "SceneDepth");
		const RGTextureHandle compositeOutputRG = m_RenderGraph.ImportRenderTarget(m_CompositeOutput, "CompositeOutput");
		std::array<RGTextureHandle, ShadowCascadeCount> shadowMapRG{};
		if (hasShadowCastingLight)
		{
			for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowCascadeCount; ++cascadeIndex)
				shadowMapRG[cascadeIndex] = m_RenderGraph.ImportRenderTarget(m_ShadowMaps[cascadeIndex], "ShadowMap");
		}

		const std::span<const uint32_t> drawStream = m_RenderScene->GetDrawStream();
		const uint32_t drawStreamCount = m_RenderScene->GetDrawStreamCount();

		m_RenderGraph.AddGraphicsPass(
			"PreDepth",
			[sceneDepthRG](RenderGraphBuilder& builder)
			{
				builder.WriteDepthAttachment(sceneDepthRG, LOAD_OP::CLEAR, STORE_OP::STORE);
			},
			[this, drawStream, drawStreamCount](GraphicsCommandList& graphics)
			{
				RenderPreDepthPass(
					graphics,
					PreDepthPassResources{ m_PreDepthPipeline, m_PreDepthBindGroup },
					PreDepthPassParameters{ drawStream, drawStreamCount });
			});

		// Each cascade targets a different RenderTargetTexture, so (unlike Sky and
		// Forward) they cannot share one Dynamic Rendering scope; each is its own
		// graph pass. They don't depend on each other, so Compile() keeps them in
		// this registration order without needing an explicit ordering hint.
		if (hasShadowCastingLight)
		{
			for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowCascadeCount; ++cascadeIndex)
			{
				const RGTextureHandle shadowMap = shadowMapRG[cascadeIndex];
				const ShadowCascadeData cascade = m_ShadowCascades[cascadeIndex];
				m_RenderGraph.AddGraphicsPass(
					"Shadow",
					[shadowMap](RenderGraphBuilder& builder)
					{
						builder.WriteDepthAttachment(shadowMap, LOAD_OP::CLEAR, STORE_OP::STORE);
					},
					[this, drawStream, drawStreamCount, cascade](GraphicsCommandList& graphics)
					{
						RenderShadowPass(
							graphics,
							ShadowPassResources{ m_ShadowPipeline },
							ShadowPassParameters{ drawStream, drawStreamCount },
							cascade);
					});
			}
		}

		// SkyPass and ForwardPass are separate logical passes registered as one
		// graph pass so they keep sharing one Dynamic Rendering scope; there is
		// no dependency between them that requires a scope boundary. Automatic
		// multi-pass scope merging is a later graph refinement, not needed here.
		m_RenderGraph.AddGraphicsPass(
			"ForwardOpaque",
			[sceneColorRG, sceneDepthRG, shadowMapRG, hasShadowCastingLight](RenderGraphBuilder& builder)
			{
				builder.WriteColorAttachment(sceneColorRG, LOAD_OP::CLEAR, STORE_OP::STORE);
				builder.WriteDepthAttachment(sceneDepthRG, LOAD_OP::LOAD, STORE_OP::STORE);
				if (hasShadowCastingLight)
				{
					for (RGTextureHandle shadowMap : shadowMapRG)
						builder.ReadTexture(shadowMap, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
				}
			},
			[this, drawStream, drawStreamCount](GraphicsCommandList& graphics)
			{
				RenderSkyPass(graphics, SkyPassResources{ m_SkyPipeline, m_SkyBindGroup });

				RenderForwardPass(
					graphics,
					ForwardPassResources{ m_GeometryPipeline, m_GeometryBindGroup },
					ForwardPassParameters{ drawStream, drawStreamCount });
			});

		// Reads SceneColor and writes the LDR CompositeOutput that
		// GetFinalPassImage() exposes.
		m_RenderGraph.AddGraphicsPass(
			"Composite",
			[sceneColorRG, compositeOutputRG](RenderGraphBuilder& builder)
			{
				builder.ReadTexture(sceneColorRG, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
				builder.WriteColorAttachment(compositeOutputRG, LOAD_OP::DONT_CARE, STORE_OP::STORE);
			},
			[this](GraphicsCommandList& graphics)
			{
				RenderCompositePass(
					graphics,
					CompositePassResources{ m_CompositePipeline, m_CompositeBindGroup },
					CompositePassParameters{ m_SceneRendererCamera.Camera.GetExposure() });
			});

		m_RenderGraph.Compile();

		CommandBuffer* commandBuffer = Renderer::GetMainCommandBuffer(m_Specification.SwapChainTarget);
		CORE_ASSERT(commandBuffer, "Invalid Forward main command buffer");
		commandBuffer->BeginRenderThread();
		m_RenderGraph.Execute(*commandBuffer, frameIndex, &Renderer::GetFrameStatistics());
		commandBuffer->EndRenderThread();
		commandBuffer->SubmitRenderThread();
	}

	void ForwardSceneRenderer::OnImGuiRender()
	{
		ImGui::Begin("Renderer");
		if (ui::PropertyGridHeader("Statistics", true))
		{
			ImGui::TextUnformatted("Active renderer: Forward");
			ImGui::Text("Mesh instances: %zu",
				m_RenderScene ? m_RenderScene->GetRasterMeshInstances().size() : size_t{ 0 });
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
				1000.0f / ImGui::GetIO().Framerate,
				ImGui::GetIO().Framerate);

			const GPUMemoryStats memoryStats = ResourceAllocator::GetStats();
			const std::string used = utils::BytesToString(memoryStats.Used);
			const std::string free = utils::BytesToString(memoryStats.Free);
			ImGui::Text("Used VRAM: %s", used.c_str());
			ImGui::Text("Free VRAM: %s", free.c_str());
			ui::EndTreeNode();
		}
		ImGui::End();
	}

	Handle<ImageView> ForwardSceneRenderer::GetFinalPassImage() const
	{
		RenderTargetTexture* compositeOutput = ResourceManager::GetRenderTarget(m_CompositeOutput);
		return compositeOutput ? compositeOutput->GetImageView() : Handle<ImageView>{};
	}
}
