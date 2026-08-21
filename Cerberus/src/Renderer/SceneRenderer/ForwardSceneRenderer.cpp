#include "pch.h"
#include "ForwardSceneRenderer.h"

#include "ImGui/ImGuiUI.h"
#include "Renderer/GraphicsCommandList.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/RendererResources.h"
#include "Renderer/ResourceAllocator.h"
#include "Renderer/ResourceManager.h"
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
		CORE_ASSERT(!shaderLibrary.Contains("ForwardPBR") && !shaderLibrary.Contains("ForwardSky"),
			"Forward renderer shaders are already owned by another renderer");

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

		RenderPassSpecification renderPassSpecification;
		renderPassSpecification.DebugName = "Forward";
		// Pipeline currently uses this flag to emit one color blend attachment
		// state per color target. The attachment itself keeps blending disabled.
		renderPassSpecification.Blend = true;
		renderPassSpecification.Attachments = {
			{ "Forward Color", ImageFormat::RGBA16F, LOAD_OP::CLEAR, STORE_OP::STORE, BlendMode::OneZero, false, true },
			{ "Forward Depth", ImageFormat::DEPTH32F, LOAD_OP::CLEAR, STORE_OP::STORE, BlendMode::None, false, true }
		};
		m_RenderPass = ResourceManager::CreateRenderPass(renderPassSpecification);

		FramebufferSpecification framebufferSpecification;
		framebufferSpecification.DebugName = "Forward";
		framebufferSpecification.RenderPass = m_RenderPass;
		framebufferSpecification.Width = m_ViewportResolution.x;
		framebufferSpecification.Height = m_ViewportResolution.y;
		framebufferSpecification.IsResizable = true;

		RenderTargetProperties colorTarget;
		colorTarget.DebugName = "Forward Color";
		colorTarget.Format = ImageFormat::RGBA16F;
		colorTarget.Width = m_ViewportResolution.x;
		colorTarget.Height = m_ViewportResolution.y;
		colorTarget.Resizable = true;
		colorTarget.ShaderResource = true;
		colorTarget.ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

		RenderTargetProperties depthTarget;
		depthTarget.DebugName = "Forward Depth";
		depthTarget.Format = ImageFormat::DEPTH32F;
		depthTarget.Width = m_ViewportResolution.x;
		depthTarget.Height = m_ViewportResolution.y;
		depthTarget.Resizable = true;
		depthTarget.ShaderResource = false;
		framebufferSpecification.RenderTargetsInfo = { colorTarget, depthTarget };
		m_Framebuffer = ResourceManager::CreateFramebuffer(framebufferSpecification);

		PipelineSpecification skyPipelineSpecification;
		skyPipelineSpecification.DebugName = "Forward Sky";
		skyPipelineSpecification.Shader = shaderLibrary.Get("ForwardSky");
		skyPipelineSpecification.RenderPass = m_RenderPass;
		skyPipelineSpecification.CullMode = CullMode::None;
		skyPipelineSpecification.DepthTest = false;
		skyPipelineSpecification.DepthWrite = false;
		m_SkyPipeline = ResourceManager::CreateGraphicsPipeline(skyPipelineSpecification);

		PipelineSpecification geometryPipelineSpecification;
		geometryPipelineSpecification.DebugName = "Forward Geometry";
		geometryPipelineSpecification.Shader = shaderLibrary.Get("ForwardPBR");
		geometryPipelineSpecification.RenderPass = m_RenderPass;
		geometryPipelineSpecification.CullMode = CullMode::Back;
		geometryPipelineSpecification.DepthOperator = DepthCompareOperator::LessOrEqual;
		geometryPipelineSpecification.DepthTest = true;
		geometryPipelineSpecification.DepthWrite = true;
		geometryPipelineSpecification.Layout = {
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal" },
			{ ShaderDataType::Float3, "a_Tangent" },
			{ ShaderDataType::Float3, "a_Binormal" },
			{ ShaderDataType::Float2, "a_TexCoord" }
		};
		m_GeometryPipeline = ResourceManager::CreateGraphicsPipeline(geometryPipelineSpecification);

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

		StorageBufferSetSpecification materialBufferSpecification;
		materialBufferSpecification.Frames = ResourceManager::GetFramesInFlight();
		materialBufferSpecification.Size = sizeof(GPUMaterialData);
		materialBufferSpecification.BindingPoint = ForwardMaterialBinding;
		materialBufferSpecification.BufferUsage = BufferUsageType::Static;
		m_MaterialBufferSet = CreateRef<StorageBufferSet>(materialBufferSpecification);

		GPUMaterialData fallbackMaterial{};
		for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetFramesInFlight(); ++frameIndex)
			m_MaterialBufferSet->Get(frameIndex)->SetData(&fallbackMaterial, sizeof(fallbackMaterial), 0);

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
			BufferBinding("ForwardLightingData", ForwardLightingBinding, VK_SHADER_STAGE_ALL)
		};
		geometryLayout.StorageBuffers = {
			BufferBinding("MaterialTable", ForwardMaterialBinding, VK_SHADER_STAGE_ALL)
		};
		geometryLayout.ImageSamplers = {
			ImageBinding("MaterialTextures", ForwardTextureBinding, MaxMaterialTextureCount, VK_SHADER_STAGE_FRAGMENT_BIT),
			ImageBinding("EnvironmentRadiance", ForwardRadianceBinding, 1, VK_SHADER_STAGE_FRAGMENT_BIT),
			ImageBinding("EnvironmentIrradiance", ForwardIrradianceBinding, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		};
		m_GeometryBindGroupLayout = ResourceManager::CreateBindGroupLayout(geometryLayout);
		m_GeometryBindGroup = ResourceManager::CreateBindGroup({ m_GeometryBindGroupLayout, DescriptorAllocationLifetime::Persistent });

		BindGroup* skyBindGroup = ResourceManager::GetBindGroup(m_SkyBindGroup);
		BindGroup* geometryBindGroup = ResourceManager::GetBindGroup(m_GeometryBindGroup);
		CORE_ASSERT(skyBindGroup && geometryBindGroup, "Failed to create Forward bind groups");
		skyBindGroup->SetUniformBuffer(m_CameraBufferSet, ForwardCameraBinding);
		skyBindGroup->SetUniformBuffer(m_LightingBufferSet, ForwardLightingBinding);
		geometryBindGroup->SetUniformBuffer(m_CameraBufferSet, ForwardCameraBinding);
		geometryBindGroup->SetUniformBuffer(m_LightingBufferSet, ForwardLightingBinding);
		geometryBindGroup->SetStorageBuffer(m_MaterialBufferSet, ForwardMaterialBinding);

		UpdateTextureTable();
		UpdateEnvironmentBindings();
	}

	void ForwardSceneRenderer::DestroyRasterResources()
	{
		if (m_GeometryBindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_GeometryBindGroup);
		if (m_SkyBindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_SkyBindGroup);
		if (m_GeometryBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_GeometryBindGroupLayout);
		if (m_SkyBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_SkyBindGroupLayout);

		if (m_GeometryPipeline.IsValid())
			ResourceManager::DestroyGraphicsPipeline(m_GeometryPipeline);
		if (m_SkyPipeline.IsValid())
			ResourceManager::DestroyGraphicsPipeline(m_SkyPipeline);
		if (m_Framebuffer.IsValid())
			ResourceManager::DestroyFramebuffer(m_Framebuffer);
		if (m_RenderPass.IsValid())
			ResourceManager::DestroyRenderPass(m_RenderPass);

		m_CameraBufferSet.reset();
		m_LightingBufferSet.reset();
		m_MaterialBufferSet.reset();

		m_GeometryBindGroup = {};
		m_SkyBindGroup = {};
		m_GeometryBindGroupLayout = {};
		m_SkyBindGroupLayout = {};
		m_GeometryPipeline = {};
		m_SkyPipeline = {};
		m_Framebuffer = {};
		m_RenderPass = {};

		auto& shaderLibrary = RendererResources::GetShaderLibrary();
		shaderLibrary.Unload("ForwardPBR");
		shaderLibrary.Unload("ForwardSky");
	}

	void ForwardSceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0 || m_ViewportResolution == glm::uvec2(width, height))
			return;

		m_ViewportResolution = { width, height };
		Framebuffer* framebuffer = ResourceManager::GetFramebuffer(m_Framebuffer);
		CORE_ASSERT(framebuffer, "Forward framebuffer is invalid");
		framebuffer->Resize(width, height);
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

	void ForwardSceneRenderer::Render(const SceneRendererCamera& sceneRendererCamera)
	{
		m_SceneRendererCamera = sceneRendererCamera;
		UpdateCameraBuffer();
		UpdateLightingBuffer();
		UpdateMaterialBuffer();
		UpdateTextureTable();
		UpdateEnvironmentBindings();

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		BindGroup* skyBindGroup = ResourceManager::GetBindGroup(m_SkyBindGroup);
		BindGroup* geometryBindGroup = ResourceManager::GetBindGroup(m_GeometryBindGroup);
		CORE_ASSERT(skyBindGroup && geometryBindGroup, "Invalid Forward bind groups");
		skyBindGroup->Update(frameIndex);
		geometryBindGroup->Update(frameIndex);

		CommandBuffer* commandBuffer = Renderer::GetMainCommandBuffer(m_Specification.SwapChainTarget);
		CORE_ASSERT(commandBuffer, "Invalid Forward main command buffer");
		commandBuffer->BeginRenderThread();

		GraphicsCommandList graphics(
			commandBuffer->GetRecordingView(frameIndex),
			&Renderer::GetFrameStatistics());
		graphics.BeginRendering(m_RenderPass, m_Framebuffer);
		graphics.DrawFullscreen(m_SkyPipeline, m_SkyBindGroup);

		DrawArea geometryDrawArea;
		geometryDrawArea.GraphicsPipeline = m_GeometryPipeline;
		geometryDrawArea.Framebuffer = m_Framebuffer;
		geometryDrawArea.BindGroup0 = m_GeometryBindGroup;
		graphics.RenderMeshInstances(
			geometryDrawArea,
			m_RenderScene->GetRasterMeshInstances(),
			m_RenderScene->GetMaterialProxies());
		graphics.EndRendering();

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
		Framebuffer* framebuffer = ResourceManager::GetFramebuffer(m_Framebuffer);
		if (!framebuffer || framebuffer->GetColorAttachmentCount() == 0)
			return {};

		RenderTargetTexture* colorTarget = ResourceManager::GetRenderTarget(framebuffer->GetColorTarget());
		return colorTarget ? colorTarget->GetImageView() : Handle<ImageView>{};
	}
}
