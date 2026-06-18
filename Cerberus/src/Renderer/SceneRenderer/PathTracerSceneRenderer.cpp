#include "pch.h"
#include "PathTracerSceneRenderer.h"
#include "Renderer/Renderer.h"
#include "Asset/AssetManager.h"
#include "Math/Matrix.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/RendererContext.h"
#include "Renderer/VulkanExtensions.h"
// TODO: Remove these to (only for test)
#include "Renderer/ShaderCompiler.h"

namespace cerberus
{
	// Temporary mode used to isolate the Vulkan RT smoke test.
	// Keep the old raster/mesh/AS mechanisms in the file, but do not execute them
	// while we validate the minimal RT path.
	static constexpr bool RT_SMOKE_TEST = true;
	static constexpr bool ENABLE_RASTER_TEST_PATH = !RT_SMOKE_TEST;

	static constexpr bool ENABLE_ACCELERATION_STRUCTURES = true;

	static constexpr uint32_t RT_OUTPUT_BINDING = 16;
	static constexpr uint32_t RT_TLAS_BINDING = 1;
	static constexpr uint32_t RT_CAMERA_BINDING = 2;

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

		// ---------------------------------------------------------------------
		// RT SMOKE TEST PATH
		// ---------------------------------------------------------------------
		// Goal: keep only the data required to validate that Vulkan RT can write
		// to a storage image and expose that image as the final renderer output.
		//
		// Intentionally disabled while RT_SMOKE_TEST is true:
		// - raster test pass
		// - Ronaldo texture test
		// - camera/material UBOs for the raster path
		// - BLAS/TLAS builds
		// - mesh rendering
		// ---------------------------------------------------------------------

		auto& shaderLibrary = Renderer::GetShaderLibrary();
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
			Ref<Shader> rtShader = Renderer::GetShaderLibrary().Get("Raytrace");
			BindGroupLayout::BindGroupLayoutInfo layoutInfoRT = ShaderReflection::ReflectLayoutInfo(rtShader, 0);
			m_RT_BindGroupLayout = ResourceManager::CreateBindGroupLayout(layoutInfoRT);

			BindGroupSpecification bindGroupRTSpec;
			bindGroupRTSpec.BindGroupLayout = m_RT_BindGroupLayout;
			bindGroupRTSpec.Persistent = false;
			m_RT_BindGroup = ResourceManager::CreateBindGroup(bindGroupRTSpec);
		}

		// RT pipeline.
		{
			PipelineSpecification rtPipelineSpec;
			rtPipelineSpec.DebugName = "RayTrace";
			rtPipelineSpec.Shader = Renderer::GetShaderLibrary().Get("Raytrace");
			rtPipelineSpec.CullMode = CullMode::Back;
			rtPipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			m_RT_Pipeline = ResourceManager::CreateRayTracingPipeline(rtPipelineSpec);
		}

		// Shader Binding Table.
		// For the smoke test the shader should only use raygen. Miss/hit groups can
		// stay allocated by the existing SBT system, but FlushRenderPasses can pass
		// empty miss/hit regions while RT_RAYGEN_ONLY is enabled.
		{
			ShaderBindingTableSpecification sbtSpec;
			sbtSpec.RayGenPrograms = { { 0, {} } };
			sbtSpec.MissPrograms = { { 1, {} } };
			sbtSpec.HitPrograms = { { 2, {} } };
			sbtSpec.Pipeline = m_RT_Pipeline;
			m_ShaderBindingTable = CreateScope<ShaderBindingTable>(sbtSpec);
		}

		// RT output image.
		{
			ImageSpecification imgSpec;
			imgSpec.DebugName = "RT Output";
			imgSpec.Width = (uint32_t)m_ViewportResolution.x;
			imgSpec.Height = (uint32_t)m_ViewportResolution.y;
			imgSpec.Format = ImageFormat::RGBA32F;
			imgSpec.Usage = ImageUsage::Storage;
			imgSpec.Flags = SAMPLE_COMPATIBLE;
			imgSpec.Mips = 1;
			imgSpec.Layers = 1;

			m_RT_OutputImage = ResourceManager::CreateImage(imgSpec);
			Image* rtImg = ResourceManager::GetImage(m_RT_OutputImage);
			CORE_ASSERT(rtImg, "Failed to create RT output image");

			rtImg->Invalidate();
			rtImg->SetLayout(
				VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

			ImageViewProperties viewProp;
			viewProp.Image = m_RT_OutputImage;
			viewProp.BaseMip = 0;
			viewProp.MipCount = 1;
			viewProp.BaseLayer = 0;
			viewProp.LayerCount = 1;
			viewProp.ViewType = ImageViewType::Image2D;

			SamplerSpecification samplerSpec;
			samplerSpec.SamplerWrap = SamplerWrap::Clamp;
			samplerSpec.SamplerFilter = SamplerFilter::Linear;
			samplerSpec.Anisotropy = false;
			samplerSpec.MinLOD = 0.0f;
			samplerSpec.MaxLOD = 1.0f;
			samplerSpec.ShaderResource = true;
			viewProp.SamplerSpec = samplerSpec;

			m_RT_OutputImageView = ResourceManager::CreateImageView(viewProp);
			ImageView* rtImgView = ResourceManager::GetImageView(m_RT_OutputImageView);
			CORE_ASSERT(rtImgView, "Failed to create RT output image view");

			rtImgView->Invalidate();
			rtImgView->UpdateDescriptor();

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
			rtBindGroup->SetImageView(m_RT_OutputImageView, RT_OUTPUT_BINDING);


		}

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

		if (m_RT_OutputImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_OutputImageView);

		if (m_RT_OutputImage.IsValid())
			ResourceManager::DestroyImage(m_RT_OutputImage);
	}

	void PathTracerSceneRenderer::BeginScene(const SceneRendererCamera& sceneRendererCamera)
	{
	}

	void PathTracerSceneRenderer::EndScene()
	{

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
		ResourceManager::ResetDescriptorPool();

		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		UpdateUBOData();

		CommandBuffer* commandBuffer = Renderer::GetMainCommandBuffer(m_Specification.SwapChainTarget);
		CORE_ASSERT(commandBuffer, "Invalid main command buffer");

		commandBuffer->RT_Begin();

		if constexpr (ENABLE_RASTER_TEST_PATH)
		{
			// Raster test path intentionally disabled while RT_SMOKE_TEST is true.
		}

		// Ray tracing dispatch: minimal raygen -> storage image smoke test.
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

		commandBuffer->RT_End();
		commandBuffer->RT_Submit();
	}

	void PathTracerSceneRenderer::SubmitMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform)
	{
	}

	void PathTracerSceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
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

		if (m_RT_OutputImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_OutputImageView);

		if (m_RT_OutputImage.IsValid())
			ResourceManager::DestroyImage(m_RT_OutputImage);

		ImageSpecification imgSpec;
		imgSpec.DebugName = "RT Output";
		imgSpec.Width = (uint32_t)m_ViewportResolution.x;
		imgSpec.Height = (uint32_t)m_ViewportResolution.y;
		imgSpec.Format = ImageFormat::RGBA32F;
		imgSpec.Usage = ImageUsage::Storage;
		imgSpec.Flags = SAMPLE_COMPATIBLE;
		imgSpec.Mips = 1;
		imgSpec.Layers = 1;

		m_RT_OutputImage = ResourceManager::CreateImage(imgSpec);
		Image* rtImg = ResourceManager::GetImage(m_RT_OutputImage);
		CORE_ASSERT(rtImg, "Failed to recreate RT output image");

		rtImg->Invalidate();
		rtImg->SetLayout(
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

		ImageViewProperties viewProp;
		viewProp.Image = m_RT_OutputImage;
		viewProp.BaseMip = 0;
		viewProp.MipCount = 1;
		viewProp.BaseLayer = 0;
		viewProp.LayerCount = 1;
		viewProp.ViewType = ImageViewType::Image2D;

		SamplerSpecification samplerSpec;
		samplerSpec.SamplerWrap = SamplerWrap::Clamp;
		samplerSpec.SamplerFilter = SamplerFilter::Linear;
		samplerSpec.Anisotropy = false;
		samplerSpec.MinLOD = 0.0f;
		samplerSpec.MaxLOD = 1.0f;
		samplerSpec.ShaderResource = true;
		viewProp.SamplerSpec = samplerSpec;

		m_RT_OutputImageView = ResourceManager::CreateImageView(viewProp);
		ImageView* rtImgView = ResourceManager::GetImageView(m_RT_OutputImageView);
		CORE_ASSERT(rtImgView, "Failed to recreate RT output image view");
		rtImgView->Invalidate();
		rtImgView->UpdateDescriptor();

		BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
		CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
		rtBindGroup->SetImageView(m_RT_OutputImageView, RT_OUTPUT_BINDING);
	}

	bool PathTracerSceneRenderer::RegisterMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		return m_RenderScene->RegisterMesh(mesh, materialTable, transform);
	}

	void PathTracerSceneRenderer::Render(const SceneRendererCamera& sceneRendererCamera)
	{
		m_SceneData.SceneRendererCamera = sceneRendererCamera;

		if constexpr (ENABLE_ACCELERATION_STRUCTURES)
		{
			CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
			m_RenderScene->PrepareRayTracing();

			// Nothing left to trace. Do not bind/trace an invalid TLAS.
			if (!m_RenderScene->HasValidTLAS())
				return;

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");

			rtBindGroup->SetAccelerationStructure(
				m_RenderScene->GetTLAS(),
				RT_TLAS_BINDING
			);
		}

		FlushRenderPasses();
	}

	bool PathTracerSceneRenderer::IsRegistered(const Ref<Mesh>& mesh)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		return m_RenderScene->IsRegistered(mesh);
	}

	bool PathTracerSceneRenderer::UnregisterMesh(const Ref<Mesh>& mesh)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		return m_RenderScene->UnregisterMesh(mesh);
	}

	void PathTracerSceneRenderer::UpdateMeshTransform(const Ref<Mesh>& mesh, const glm::mat4& transform)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		m_RenderScene->UpdateMeshTransform(mesh, transform);
	}

	void PathTracerSceneRenderer::OnImGuiRender()
	{
	}

	Handle<ImageView> PathTracerSceneRenderer::GetFinalPassImage() const
	{
		return m_RT_OutputImageView;
	}
}