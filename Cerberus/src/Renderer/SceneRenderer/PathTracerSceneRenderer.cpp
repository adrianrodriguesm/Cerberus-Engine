#include "pch.h"
#include "PathTracerSceneRenderer.h"
#include "Renderer/Renderer.h"
#include "Asset/AssetManager.h"
#include "Math/Matrix.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/RenderQueueManager.h"
// TODO: Remove these to (only for test)
#include "Renderer/ShaderCompiler.h"
#include "Renderer/TextureAsset.h"
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
		m_Specification = spec;
		m_ViewportResolution = spec.Resolution;
		if (m_ViewportResolution.x <= 0 || m_ViewportResolution.y <= 0)
			m_ViewportResolution = { 1920, 1080 };

		auto& shaderLibrary = Renderer::GetShaderLibrary();
		shaderLibrary.Load("Resources/Shaders/Ray Tracing/Raytrace.rgen", true);

		RenderQueueManager::ExecuteRenderQueue();
		// Pipeline, RenderPass, Framebuffer, and BindGroup/BindGroupLayout creation
		{
			// Render Pass
			RenderPassSpecification renderPassSpec;
			renderPassSpec.DebugName = "RayTraceTest";
			renderPassSpec.Attachments = { {.DebugName = "ColorAttachment", .Format = ImageFormat::RGBA,
											.LoadOP = LOAD_OP::CLEAR, .StoreOP = STORE_OP::STORE,
											.BlendEnabled = false, .IsRenderTarget = true }, 
											{.DebugName = "DepthAttachment", .Format = ImageFormat::DEPTH24STENCIL8,
											.LoadOP = LOAD_OP::CLEAR, .StoreOP = STORE_OP::STORE,
											.BlendEnabled = false, .IsRenderTarget = true } };

			m_RenderPass = ResourceManager::CreateRenderPass(renderPassSpec);


			// Pipeline
			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "RayTraceTest";
			Ref<Shader> shader = Renderer::GetShaderLibrary().Get("RayTraceTest");
			pipelineSpec.Shader = shader;
			pipelineSpec.Layout = { { ShaderDataType::Float3, "a_Position"	},
									{ ShaderDataType::Float3, "a_Normal"	},
									{ ShaderDataType::Float3, "a_Tangent"	},

									{ ShaderDataType::Float3, "a_Binormal"	},
									{ ShaderDataType::Float2, "a_TexCoord"	}, };
			pipelineSpec.CullMode = CullMode::Back;
			pipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			// -- Render to the same target texture as the geometry pass
			pipelineSpec.RenderPass = m_RenderPass;
			m_Pipeline = ResourceManager::CreateGraphicsPipeline(pipelineSpec);

			// SET 0
			// BindGroup/BindGroupLayout
			BindGroupLayout::BindGroupLayoutInfo layoutInfo0 = ShaderReflection::ReflectLayoutInfo(shader, 0);
			m_BindGroup0Layout = ResourceManager::CreateBindGroupLayout(layoutInfo0);


			BindGroupSpecification bindGroup0Spec;
			bindGroup0Spec.BindGroupLayout = m_BindGroup0Layout;
			bindGroup0Spec.Persistent = false;
			m_BindGroup0 = ResourceManager::CreateBindGroup(bindGroup0Spec);

			// SET 1
			// BindGroup/BindGroupLayout
			BindGroupLayout::BindGroupLayoutInfo layoutInfo1 = ShaderReflection::ReflectLayoutInfo(shader, 1);
			m_BindGroup1Layout = ResourceManager::CreateBindGroupLayout(layoutInfo1);

			BindGroupSpecification bindGroup1Spec;
			bindGroup1Spec.BindGroupLayout = m_BindGroup1Layout;
			bindGroup1Spec.Persistent = false;
			m_BindGroup1 = ResourceManager::CreateBindGroup(bindGroup1Spec);

			// SET 2
			// Ray Tracing
			BindGroupLayout::BindGroupLayoutInfo layoutInfoRT = ShaderReflection::ReflectLayoutInfo(shader, 2);
			m_RT_BindGroupLayout = ResourceManager::CreateBindGroupLayout(layoutInfoRT);

			BindGroupSpecification bindGroupRTSpec;
			bindGroupRTSpec.BindGroupLayout = m_RT_BindGroupLayout;
			bindGroupRTSpec.Persistent = false;
			m_RT_BindGroup = ResourceManager::CreateBindGroup(bindGroupRTSpec);


			// Pipeline
			PipelineSpecification rt_pipelineSpec;
			rt_pipelineSpec.DebugName = "RayTrace";
			rt_pipelineSpec.Shader = Renderer::GetShaderLibrary().Get("Raytracer");
			rt_pipelineSpec.CullMode = CullMode::Back;
			rt_pipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			// -- Render to the same target texture as the geometry pass
			rt_pipelineSpec.RenderPass = m_RenderPass;
			m_RT_Pipeline = ResourceManager::CreateRayTracingPipeline(rt_pipelineSpec);

			ShaderBindingTableSpecification sbtSpec;
			sbtSpec.RayGenPrograms = { {0 , {}} };
			sbtSpec.MissPrograms = { {1 , {}} };
			sbtSpec.HitPrograms = { {2 , {}} };
			sbtSpec.Pipeline = m_RT_Pipeline;
			m_ShaderBindingTable = CreateScope<ShaderBindingTable>(sbtSpec);

			// Framebuffer
			FramebufferSpecification preDepthFramebufferSpec;
			preDepthFramebufferSpec.DebugName = "RayTrace Framebuffer";
			preDepthFramebufferSpec.RenderPass = m_RenderPass;
			preDepthFramebufferSpec.Width = m_ViewportResolution.x;
			preDepthFramebufferSpec.Height = m_ViewportResolution.y;

			RenderTargetProperties rtPropertiesColor;
			rtPropertiesColor.DebugName = "Color Target";
			rtPropertiesColor.Format = ImageFormat::RGBA;
			rtPropertiesColor.ShaderResource = true;
			rtPropertiesColor.BaseMip = 0;
			rtPropertiesColor.MipCount = 1;
			rtPropertiesColor.BaseLayer = 0;
			rtPropertiesColor.LayerCount = 1;
			rtPropertiesColor.Resizable = true;
			rtPropertiesColor.Height = m_ViewportResolution.y;
			rtPropertiesColor.Width = m_ViewportResolution.x;

			RenderTargetProperties rtPropertiesDepth;
			rtPropertiesDepth.DebugName = "Depth Target";
			rtPropertiesDepth.Format = ImageFormat::DEPTH24STENCIL8;
			rtPropertiesDepth.ShaderResource = true;
			rtPropertiesDepth.BaseMip = 0;
			rtPropertiesDepth.MipCount = 1;
			rtPropertiesDepth.BaseLayer = 0;
			rtPropertiesDepth.LayerCount = 1;
			rtPropertiesDepth.Resizable = true;
			rtPropertiesDepth.Height = m_ViewportResolution.y;
			rtPropertiesDepth.Width = m_ViewportResolution.x;

			preDepthFramebufferSpec.RenderTargetsInfo = { rtPropertiesColor, rtPropertiesDepth };

			m_Framebuffer = ResourceManager::CreateFramebuffer(preDepthFramebufferSpec);
		}

		// Bing group Test
		{
			// Texture
			TextureMetadata texMetadata;
			texMetadata.FilePath = "Resources/Textures/Ronaldo.jpg";

			m_TextureAsset = CreateScope<TextureAsset>(texMetadata);

			TextureProperties texProp;
			texProp.GenerateMips = true;
			m_TextureRonaldo = ResourceManager::CreateTexture2D(m_TextureAsset->GetFormat(), m_TextureAsset->GetWidth(),
				m_TextureAsset->GetHeight(), m_TextureAsset->GetWriteableBuffer().Data, texProp);

			Texture2D* texture = ResourceManager::GetTexture2D(m_TextureRonaldo);

			// UBO
			UniformBufferSetSpecification uboSetSpec;
			uboSetSpec.Frames = ResourceManager::GetRendererConfig().FramesInFlight;
			uboSetSpec.Size = 100;
			uboSetSpec.BindingPoint = 2;
			m_UniformBufferSet = CreateRef<UniformBufferSet>(uboSetSpec);

			uint32_t framesInFlights = ResourceManager::GetFramesInFlight();

			struct RendererData
			{
				glm::vec4 Color = glm::vec4(1.f, 1.f, 1.f, 1.f);
			};

			RendererData data;
			data.Color = glm::vec4(1.f, 0.f, 0.f, 1.f);

			for (uint32_t frameIndex = 0; frameIndex < framesInFlights; frameIndex++)
			{
				Ref<UniformBuffer> ubo = m_UniformBufferSet->Get(frameIndex);
				ubo->SetData(&data, sizeof(RendererData), 0);
			}

			// Apply to bind group
			BindGroup* bindGroup0 = ResourceManager::GetBindGroup(m_BindGroup0);
			bindGroup0->SetImageView(texture->GetImageView(), 0);
			bindGroup0->SetUniformBuffer(m_UniformBufferSet, 2);

			// UBO
			UniformBufferSetSpecification uboSetCamSpec;
			uboSetCamSpec.Frames = ResourceManager::GetRendererConfig().FramesInFlight;
			uboSetCamSpec.Size = sizeof(CameraUB);
			uboSetCamSpec.BindingPoint = 0;
			m_UniformBufferSetCamera = CreateRef<UniformBufferSet>(uboSetCamSpec);


			// SSBO
			BindGroup* bindGroup1 = ResourceManager::GetBindGroup(m_BindGroup1);
			bindGroup1->SetUniformBuffer(m_UniformBufferSetCamera, 0);
		}

		RenderQueueManager::ExecuteRenderQueue();
	}

	PathTracerSceneRenderer::~PathTracerSceneRenderer()
	{
		if (m_MeshRegister.ASRegisterInfo.TLAS.IsValid())
			ResourceManager::DestroyAccelerationStructure(m_MeshRegister.ASRegisterInfo.TLAS);
		ResourceManager::DestroyTexture2D(m_TextureRonaldo);
		ResourceManager::DestroyFramebuffer(m_Framebuffer);
		ResourceManager::DestroyGraphicsPipeline(m_Pipeline);
		ResourceManager::DestroyRayTracingPipeline(m_RT_Pipeline);
		ResourceManager::DestroyRenderPass(m_RenderPass);
		ResourceManager::DestroyBindGroup(m_BindGroup0);
		ResourceManager::DestroyBindGroupLayout(m_BindGroup0Layout);
		ResourceManager::DestroyBindGroup(m_BindGroup1);
		ResourceManager::DestroyBindGroupLayout(m_BindGroup1Layout);
		ResourceManager::DestroyBindGroup(m_RT_BindGroup);
		ResourceManager::DestroyBindGroupLayout(m_RT_BindGroupLayout);
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
			glm::mat4 inverseViewProjection = cameraUBO.InverseView * cameraUBO.InverseProjection;
			cameraUBO.InverseViewReverseProjection = cameraUBO.InverseView * glm::inverse(m_SceneData.SceneRendererCamera.Camera.GetReverseProjection());
			Ref<UniformBuffer> ubo = m_UniformBufferSetCamera->Get(frameIndex);
			ubo->SetData(&cameraUBO, sizeof(CameraUB), 0);
		}

		// Apply to bind group
		BindGroup* bindGroup1 = ResourceManager::GetBindGroup(m_BindGroup1);
		bindGroup1->Update(frameIndex);

		// TODO: This shouldn't be necessary (at least every frame) for set 0
		BindGroup* bindGroup0 = ResourceManager::GetBindGroup(m_BindGroup0);
		bindGroup0->Update(frameIndex);

	}

	void PathTracerSceneRenderer::FlushRenderPasses()
	{
		// Render Frame
		ResourceManager::ResetDescriptorPool();
		UpdateUBOData();
		CommandBuffer* commnadBuffer = Renderer::GetMainCommandBuffer(m_Specification.SwapChainTarget);
		commnadBuffer->RT_Begin();
		{
			DrawArea drawArea;
			drawArea.GraphicsPipeline = m_Pipeline;
			drawArea.Framebuffer = m_Framebuffer;
			drawArea.BindGroup0 = m_BindGroup0;
			drawArea.BindGroup1 = m_BindGroup1;
			drawArea.DrawCount = m_MeshRegister.MeshInstanceCount;
			Renderer::BeginRenderPass(commnadBuffer, m_RenderPass, drawArea);
			for (auto& [meshSourceHandle, submeshMap] : m_MeshRegister.MeshInstanceMap)
			{
				MeshSourceProxy* meshSource = ResourceManager::GetMeshSourceProxy(meshSourceHandle);
				Handle<GPUBuffer> hVertexBuffer = meshSource->VertexBuffer->GetGPUBuffer();
				Handle<GPUBuffer> hIndexBuffer = meshSource->IndexBuffer->GetGPUBuffer();
				Renderer::SetActiveVertexAndIndexBuffer(commnadBuffer, hVertexBuffer, hIndexBuffer);
				for (auto& [submeshIndex, meshIntanceList] : submeshMap)
				{
					Renderer::RenderMeshInstances(commnadBuffer, drawArea, meshIntanceList);
				}
			}
			Renderer::EndRenderPass(commnadBuffer);
		}
		commnadBuffer->RT_End();
		commnadBuffer->RT_Submit();
	}

	void PathTracerSceneRenderer::SubmitMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4 & transform)
	{
	}

	void PathTracerSceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (m_ViewportResolution.x != width || m_ViewportResolution.y != height)
		{
			m_ViewportResolution.x = width;
			m_ViewportResolution.y = height;

			Framebuffer* framebuffer = ResourceManager::GetFramebuffer(m_Framebuffer);
			framebuffer->Resize(width, height);
		}
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
		//Renderer::ExecuteRenderQueue();
		// Build Bottom Level Acceleration Structure if needed
#if 1
 		if (m_MeshRegister.ASRegisterInfo.BLASToBuild.size() > 0)
		{
			AccelerationStructureBuilder asBuilder;
			asBuilder.BeginBLASBuild(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
			for (uint32_t i = 0; i < m_MeshRegister.ASRegisterInfo.BLASToBuild.size(); i++)
			{
				const auto& meshSourceProxy = m_MeshRegister.ASRegisterInfo.BLASToBuild[i];
				asBuilder.SummitMesh(meshSourceProxy);
			}
			asBuilder.EndBLASBuild();
			m_MeshRegister.ASRegisterInfo.BLASToBuild.clear();
		}
		// Build or rebuild the Top Level Acceleration Structure if needed
		if (!m_MeshRegister.ASRegisterInfo.TLAS.IsValid() || m_MeshRegister.ASRegisterInfo.UpdateTLAS)
		{
			AccelerationStructureBuilder asBuilder;

			asBuilder.BeginTLASBuild(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
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
#endif
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
		Framebuffer* framebuffer = ResourceManager::GetFramebuffer(m_Framebuffer);
		Handle<RenderTargetTexture> rtHandle = framebuffer->GetColorTarget(0);
		RenderTargetTexture* renderTarget = ResourceManager::GetRenderTarget(rtHandle);
		Handle<ImageView> imgViewHandle = renderTarget->GetImageView();
		return imgViewHandle;
	}
}
