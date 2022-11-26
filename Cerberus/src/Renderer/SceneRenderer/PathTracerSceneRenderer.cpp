#include "pch.h"
#include "PathTracerSceneRenderer.h"
#include "Renderer/Renderer.h"
#include "Asset/AssetManager.h"
#include "Math/Matrix.h"
#include "Renderer/ResourceManager.h"
namespace cerberus
{

	PathTracerSceneRenderer::PathTracerSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec)
	{

		if (m_Specification.SwapChainTarget)
			m_CommandBuffer = CommandBuffer::CreateFromSwapchain();
		else
			m_CommandBuffer = CommandBuffer::Create(Renderer::GetRendererConfig().FramesInFlight, "SceneRenderer");


		// Test
		{
			FramebufferSpecification preDepthFramebufferSpec;
			preDepthFramebufferSpec.AttachmentsSpecification = { ImageFormat::RGBA };
			preDepthFramebufferSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			preDepthFramebufferSpec.DebugName = "RayTraceTest";

			RenderPassSpecification preDepthRenderPassSpec;
			preDepthRenderPassSpec.TargetFramebuffer = CreateRef<Framebuffer>(preDepthFramebufferSpec);
			preDepthRenderPassSpec.DebugName = "RayTraceTest";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "RayTraceTest";
			Ref<Shader> shader = Renderer::GetShaderLibrary().Get("RayTraceTest");
			pipelineSpec.Shader = shader;
			pipelineSpec.Layout = {};
			pipelineSpec.CullMode = CullMode::Back;
			pipelineSpec.DepthTest = false;
			pipelineSpec.DepthWrite = false;
			// -- Render to the same target texture as the geometry pass
			pipelineSpec.RenderPass = CreateRef<RenderPass>(preDepthRenderPassSpec);
			m_Pipeline = CreateRef<Pipeline>(pipelineSpec);
			m_Material = CreateRef<Material>(shader, "RayTraceTest");
		}

		AssetHandle cubeHandle = AssetManager::GetAssetHandleFromFilePath(AssetManager::GetAssetPath().string() + "/Meshes/Default/Cube.cmesh");
		//m_Cube = AssetManager::GetAsset<Mesh>(cubeHandle);
		AccelerationStructureBuilder asBuilder();

		Renderer::ExecuteRenderQueue();
		/** /
		m_ASBuilder->BeginBLASBuild( VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
		m_ASBuilder->SummitMesh(m_Cube);
		std::vector<Ref<BottomLevelAccelerationStructure>> blas;
		m_ASBuilder->EndBLASBuild(blas);
		Renderer::ExecuteRenderQueue();
		/** /
		m_ASBuilder->BeginTLASBuild(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
		m_ASBuilder->SubmitMeshInstance(m_Cube, math::MatFactory::CreateIdentityMat4());
		m_ASBuilder->EndTLASBuild();
		/**/
		Renderer::ExecuteRenderQueue();
		// UniformBuffer;
		m_UniformBufferSet = CreateRef<UniformBufferSet>(Renderer::GetRendererConfig().FramesInFlight);

	}

	PathTracerSceneRenderer::~PathTracerSceneRenderer()
	{
	}

	void PathTracerSceneRenderer::BeginScene(const SceneRendererCamera& sceneRendererCamera)
	{
	}

	void PathTracerSceneRenderer::EndScene()
	{
		m_CommandBuffer->Begin();
		// Composite pass
		Renderer::BeginRenderPass(m_CommandBuffer, m_Pipeline->GetSpecification().RenderPass, true);
		{
			Renderer::RenderFullscreenQuad(m_CommandBuffer, m_Pipeline, m_UniformBufferSet, m_Material);
		}
		Renderer::EndRenderPass(m_CommandBuffer);
		m_CommandBuffer->End();
		m_CommandBuffer->Submit();
	}

	void PathTracerSceneRenderer::SubmitMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4 & transform)
	{
	}

	void PathTracerSceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
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
			// Create the mesh source proxy
			meshProxyHandle = ResourceManager::CreateMeshSourceProxy(vertices.data(), vertices.size() * sizeof(MeshSource::Vertex), indices.data(), indices.size() * sizeof(MeshSource::Index));
			// Add the submesh info 
			// TODO: Move this code to the MeshSourceProxy constructor
			MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshProxyHandle);
			const auto& submeshes = meshSource->GetSubmeshes();
			meshSourceProxy->Submeshes.reserve(submeshes.size());
			for(const auto& submesh : submeshes)
			{
				SubmeshSourceProxy& submeshProxy = meshSourceProxy->Submeshes.emplace_back();
				submeshProxy.IndexCount = submesh.IndexCount;
				submeshProxy.BaseIndex = submesh.BaseIndex;
				submeshProxy.BaseVertex = submesh.BaseVertex;
				submeshProxy.MaterialIndex = submesh.MaterialIndex;
				submeshProxy.Transform = submesh.Transform;
			}

			// Added to the register
			m_MeshRegister.MeshSourceRegistered[meshSourceAssetHandle] = meshProxyHandle;
			m_MeshRegister.ASRegisterInfo.BLASToBuild.push_back(meshProxyHandle);
		}
		else
		{
			meshProxyHandle = m_MeshRegister.MeshSourceRegistered[meshSourceAssetHandle];
		}

		// Create the actual mesh instances
		MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshProxyHandle);
		const auto& submeshesProxy = meshSourceProxy->Submeshes;

		auto& meshInstanceProxies = m_MeshRegister.MeshInstanceRegistered[mesh->Handle];
		//meshInstanceProxies.reserve(submeshesProxy.size());
		
		m_MeshRegister.ASRegisterInfo.UpdateTLAS = true;
		for (uint32_t submeshIndex = 0; submeshIndex < submeshesProxy.size(); submeshIndex++)
		{
			Handle<MeshInstanceProxy> meshInstanceHandle = ResourceManager::CreateMeshInstanceProxy(meshProxyHandle, submeshIndex, materialTable->GetMaterial(submeshesProxy[submeshIndex].MaterialIndex));
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstanceHandle);
			meshInstance->WorldTransform = transform * submeshesProxy[submeshIndex].Transform;

			meshInstanceProxies.push_back(meshInstanceHandle);
		}
		
		return true;
	}

	void PathTracerSceneRenderer::Render(const SceneRendererCamera& sceneRendererCamera)
	{
		Renderer::ExecuteRenderQueue();
		// Build Bottom Level Acceleration Structure if needed
		if (m_MeshRegister.ASRegisterInfo.BLASToBuild.size() > 0)
		{
			AccelerationStructureBuilder asBuilder;
			asBuilder.BeginBLASBuild(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
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
			asBuilder.BeginTLASBuild(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
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

	bool PathTracerSceneRenderer::IsRegistered(const Ref<Mesh>& mesh)
	{
		if (m_MeshRegister.MeshInstanceRegistered.find(mesh->Handle) == m_MeshRegister.MeshInstanceRegistered.end())
			return false;

		return true;
	}

	bool PathTracerSceneRenderer::UnregisterMesh(const Ref<Mesh>& mesh)
	{
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

	Ref<Image2D> PathTracerSceneRenderer::GetFinalPassImage() const
	{
		return m_Pipeline->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->GetImage(0);;
	}
}

