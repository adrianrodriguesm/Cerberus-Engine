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
#include "Renderer/TextureAsset.h"
namespace
{
	// Temporary mode used to isolate the Vulkan RT smoke test.
	// Keep the old raster/mesh/AS mechanisms in the file, but do not execute them
	// while we validate the minimal RT path.
	static constexpr bool RT_SMOKE_TEST = true;
	static constexpr bool ENABLE_RASTER_TEST_PATH = !RT_SMOKE_TEST;
	static constexpr bool ENABLE_ACCELERATION_STRUCTURES = false;

	// Current Raytrace.rgen was using binding 16.
	// If you simplify the shader to binding 0, change this constant to 0.
	static constexpr uint32_t RT_OUTPUT_BINDING = 16;
	static constexpr bool RT_RAYGEN_ONLY = true;
}
namespace cerberus
{
	/** /
	* Bind groups / DescriptorSets
	* Set 0 = Fully static data (e.g Enviroment Maps)
	* Set 1 = Per Frame data (e.g shadow map)
	* Set 2 = Per render pass data (e.g to be determine)
	* Set 3 = Per draw data (e.g. materials)
	/**/
	PathTracerSceneRenderer::PathTracerSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec)
	{
		(void)scene;

		m_Specification = spec;
		m_ViewportResolution = spec.Resolution;
		if (m_ViewportResolution.x <= 0 || m_ViewportResolution.y <= 0)
			m_ViewportResolution = { 1920, 1080 };

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
		shaderLibrary.Load("Resources/Shaders/Ray Tracing/Raytrace.rgen", true);

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
		if (m_MeshRegister.ASRegisterInfo.TLAS.IsValid())
			ResourceManager::DestroyAccelerationStructure(m_MeshRegister.ASRegisterInfo.TLAS);

		if (m_TextureRonaldo.IsValid())
			ResourceManager::DestroyTexture2D(m_TextureRonaldo);

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

		if constexpr (ENABLE_RASTER_TEST_PATH)
		{
			CameraUB cameraUBO;
			cameraUBO.Projection = m_SceneData.SceneRendererCamera.Camera.GetProjection();
			cameraUBO.View = m_SceneData.SceneRendererCamera.ViewMatrix;
			cameraUBO.ViewProjection = cameraUBO.Projection * cameraUBO.View;
			cameraUBO.InverseView = glm::inverse(cameraUBO.View);
			cameraUBO.InverseProjection = glm::inverse(cameraUBO.Projection);
			cameraUBO.InverseViewReverseProjection =
				cameraUBO.InverseView * glm::inverse(m_SceneData.SceneRendererCamera.Camera.GetReverseProjection());

			Ref<UniformBuffer> ubo = m_UniformBufferSetCamera->Get(frameIndex);
			ubo->SetData(&cameraUBO, sizeof(CameraUB), 0);

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
		// if the mesh is already registered return false
		if (m_MeshRegister.MeshInstanceRegistered.find(mesh->Handle) != m_MeshRegister.MeshInstanceRegistered.end())
			return false;
		// Check if the mesh source is already registered
		Ref<MeshSource> meshSource = mesh->GetMeshSource();
		MeshSourceHandle meshSourceAssetHandle = mesh->GetMeshSource()->Handle;
		Handle<MeshSourceProxy> meshProxyHandle;

		if (m_MeshRegister.MeshSourceRegistered.find(meshSourceAssetHandle) == m_MeshRegister.MeshSourceRegistered.end())
		{
			const auto& indices = meshSource->GetIndices();
			const auto& vertices = meshSource->GetVertices();
			const auto& submeshes = meshSource->GetSubmeshes();
			// Create the mesh source proxy
			meshProxyHandle = ResourceManager::CreateMeshSourceProxy(vertices.data(), vertices.size() * sizeof(MeshSource::Vertex), indices.data(), indices.size() * sizeof(MeshSource::Index), submeshes);
			// Added to the register
			m_MeshRegister.MeshSourceRegistered[meshSourceAssetHandle] = meshProxyHandle;
			m_MeshRegister.ASRegisterInfo.BLASToBuild.push_back(meshProxyHandle);
			m_MeshRegister.InstancePerMeshSourceRegistered[meshSourceAssetHandle] = {};
			MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshProxyHandle);

			std::unordered_map<uint32_t, std::vector<Handle<MeshInstanceProxy>>> submeshesInstances;
			submeshesInstances.reserve(meshSourceProxy->Submeshes.size());
			for (uint32_t i = 0; i < meshSourceProxy->Submeshes.size(); i++)
			{
				submeshesInstances[i] = std::vector<Handle<MeshInstanceProxy>>();
			}

			m_MeshRegister.MeshInstanceMap[meshProxyHandle] = submeshesInstances;
		}
		else
		{
			meshProxyHandle = m_MeshRegister.MeshSourceRegistered[meshSourceAssetHandle];
		}

		m_MeshRegister.InstancePerMeshSourceRegistered[meshSourceAssetHandle].push_back(mesh->Handle);
		// Create the actual mesh instances
		MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshProxyHandle);
		const auto& submeshesProxy = meshSourceProxy->Submeshes;

		auto& meshInstanceProxies = m_MeshRegister.MeshInstanceRegistered[mesh->Handle];

		m_MeshRegister.ASRegisterInfo.UpdateTLAS = true;
		for (uint32_t submeshIndex = 0; submeshIndex < submeshesProxy.size(); submeshIndex++)
		{
			Handle<MeshInstanceProxy> meshInstanceHandle = ResourceManager::CreateMeshInstanceProxy(meshProxyHandle, submeshIndex);
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstanceHandle);
			meshInstance->WorldTransform = transform * submeshesProxy[submeshIndex].Transform;

			meshInstanceProxies.push_back(meshInstanceHandle);

			m_MeshRegister.MeshInstanceMap[meshProxyHandle][submeshIndex].push_back(meshInstanceHandle);
			m_MeshRegister.MeshInstanceCount++;
		}

		return true;
	}

	void PathTracerSceneRenderer::Render(const SceneRendererCamera& sceneRendererCamera)
	{
		m_SceneData.SceneRendererCamera = sceneRendererCamera;

		if constexpr (ENABLE_ACCELERATION_STRUCTURES)
		{
			// Build Bottom Level Acceleration Structure if needed.
			if (m_MeshRegister.ASRegisterInfo.BLASToBuild.size() > 0)
			{
				AccelerationStructureBuilder asBuilder;
				asBuilder.BeginBLASBuild(
					VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR |
					VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
					VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);

				for (uint32_t i = 0; i < m_MeshRegister.ASRegisterInfo.BLASToBuild.size(); i++)
				{
					const auto& meshSourceProxy = m_MeshRegister.ASRegisterInfo.BLASToBuild[i];
					asBuilder.SummitMesh(meshSourceProxy);
				}

				asBuilder.EndBLASBuild();
				m_MeshRegister.ASRegisterInfo.BLASToBuild.clear();
			}

			// Build or rebuild the Top Level Acceleration Structure if needed.
			if (!m_MeshRegister.ASRegisterInfo.TLAS.IsValid() || m_MeshRegister.ASRegisterInfo.UpdateTLAS)
			{
				AccelerationStructureBuilder asBuilder;
				asBuilder.BeginTLASBuild(
					VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
					VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);

				for (const auto& [meshAssetHandle, meshesInstances] : m_MeshRegister.MeshInstanceRegistered)
				{
					for (uint32_t i = 0; i < meshesInstances.size(); i++)
					{
						const auto& meshInstance = meshesInstances[i];
						asBuilder.SubmitMeshInstance(meshInstance);
					}
				}

				asBuilder.EndTLASBuild(m_MeshRegister.ASRegisterInfo.TLAS);
				m_MeshRegister.ASRegisterInfo.UpdateTLAS = false;
			}
		}

		FlushRenderPasses();
	}

	bool PathTracerSceneRenderer::IsRegistered(const Ref<Mesh>& mesh)
	{
		if (m_MeshRegister.MeshInstanceRegistered.find(mesh->Handle) == m_MeshRegister.MeshInstanceRegistered.end())
			return false;

		return true;
	}

	bool PathTracerSceneRenderer::UnregisterMesh(const Ref<Mesh>& mesh)
	{
		bool unregistered = false;
		// Update TLAS
		m_MeshRegister.ASRegisterInfo.UpdateTLAS = true;
		// Destroy Mesh Instance
		if (m_MeshRegister.MeshInstanceRegistered.find(mesh->Handle) != m_MeshRegister.MeshInstanceRegistered.end())
		{
			std::vector<Handle<MeshInstanceProxy>> hInstanceProxyList = m_MeshRegister.MeshInstanceRegistered[mesh->Handle];
			m_MeshRegister.MeshInstanceRegistered.erase(mesh->Handle);
			Ref<MeshSource> meshSourceAsset = mesh->GetMeshSource();

			auto& vec = m_MeshRegister.InstancePerMeshSourceRegistered[meshSourceAsset->Handle];
			vec.erase(std::remove(vec.begin(), vec.end(), mesh->Handle), vec.end());

			Handle<MeshSourceProxy> hMeshSource = m_MeshRegister.MeshSourceRegistered[meshSourceAsset->Handle];
			for (Handle<MeshInstanceProxy> hMeshInstance : hInstanceProxyList)
			{
				MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(hMeshInstance);
				std::vector<Handle<MeshInstanceProxy>>& hMeshInstancePerSubmeshIndex = m_MeshRegister.MeshInstanceMap[hMeshSource][meshInstance->SubmeshIndex];
				for (uint32_t index = 0; index < hMeshInstancePerSubmeshIndex.size(); index++)
				{
					Handle<MeshInstanceProxy> hCurrMeshInstance = hMeshInstancePerSubmeshIndex[index];
					if (hMeshInstance == hCurrMeshInstance)
					{
						hMeshInstancePerSubmeshIndex.erase(hMeshInstancePerSubmeshIndex.begin() + index);
						m_MeshRegister.MeshInstanceCount--;
						break;
					}
				}


				ResourceManager::DestroyMeshInstanceProxy(hMeshInstance);
			}

			// Destory mesh source proxy
			if (m_MeshRegister.InstancePerMeshSourceRegistered[meshSourceAsset->Handle].size() == 0)
			{
				m_MeshRegister.MeshInstanceMap.erase(hMeshSource);
				m_MeshRegister.InstancePerMeshSourceRegistered.erase(meshSourceAsset->Handle);
				m_MeshRegister.MeshSourceRegistered.erase(meshSourceAsset->Handle);

				ResourceManager::DestroyMeshSourceProxy(hMeshSource);
			}

		}
		else
		{
			return false;
		}


		return true;
	}
	void PathTracerSceneRenderer::UpdateMeshTransform(const Ref<Mesh>& mesh, const glm::mat4& transform)
	{
		if (!IsRegistered(mesh))
			return;

		m_MeshRegister.ASRegisterInfo.UpdateTLAS = true;
		std::vector<Handle<MeshInstanceProxy>> meshInstances = m_MeshRegister.MeshInstanceRegistered[mesh->Handle];
		Handle<MeshSourceProxy> meshSourceHandle = m_MeshRegister.MeshSourceRegistered[mesh->GetMeshSource()->Handle];
		MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshSourceHandle);
		for (uint32_t submeshIndex = 0; submeshIndex < meshSourceProxy->Submeshes.size(); submeshIndex++)
		{
			const SubmeshSourceProxy& submeshProxy = meshSourceProxy->Submeshes[submeshIndex];
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstances[submeshIndex]);
			meshInstance->WorldTransform = transform * submeshProxy.Transform;
		}

	}

	void PathTracerSceneRenderer::OnImGuiRender()
	{
	}

	Handle<ImageView> PathTracerSceneRenderer::GetFinalPassImage() const
	{
		return m_RT_OutputImageView;
	}
}