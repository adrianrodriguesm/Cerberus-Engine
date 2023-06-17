#include "pch.h"
#include "PathTracerSceneRenderer.h"
#include "Renderer/Renderer.h"
#include "Asset/AssetManager.h"
#include "Math/Matrix.h"
#include "Renderer/ResourceManager.h"
// TODO: Remove these to (only for test)
#include "Renderer/ShaderCompiler.h"
#include "Renderer/TextureAsset.h"
namespace cerberus
{

	PathTracerSceneRenderer::PathTracerSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec)
	{
		// Pipeline, RenderPass, Framebuffer, and BindGroup/BindGroupLayout creation
		{
			// Render Pass
			RenderPassSpecification renderPassSpec;
			renderPassSpec.DebugName = "RayTraceTest";
			renderPassSpec.Attachments = { {.DebugName = "ColorAttachment", .Format = ImageFormat::RGBA,
											.LoadOP = LOAD_OP::CLEAR, .StoreOP = STORE_OP::STORE,
											.BlendEnabled = false, .IsRenderTarget = true } };

			m_RenderPass = ResourceManager::CreateRenderPass(renderPassSpec);

			// Pipeline
			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "RayTraceTest";
			Ref<Shader> shader = Renderer::GetShaderLibrary().Get("RayTraceTest");
			pipelineSpec.Shader = shader;
			pipelineSpec.Layout = {};
			pipelineSpec.CullMode = CullMode::Back;
			pipelineSpec.DepthTest = false;
			pipelineSpec.DepthWrite = false;
			// -- Render to the same target texture as the geometry pass
			pipelineSpec.RenderPass = m_RenderPass;
			m_Pipeline = ResourceManager::CreateGraphicsPipeline(pipelineSpec);

			// BindGroup/BindGroupLayout
			BindGroupLayout::BindGroupLayoutInfo layoutInfo = ShaderReflection::ReflectLayoutInfo(shader, 0);
			m_RTBindGroupLayout = ResourceManager::CreateBindGroupLayout(layoutInfo);

			BindGroupSpecification bindGroupSpec;
			bindGroupSpec.BindGroupLayout = m_RTBindGroupLayout;
			bindGroupSpec.Persistent = false;

			m_RTBindGroup = ResourceManager::CreateBindGroup(bindGroupSpec);

			// Framebuffer
			FramebufferSpecification preDepthFramebufferSpec;
			preDepthFramebufferSpec.DebugName = "RayTrace Framebuffer";
			preDepthFramebufferSpec.RenderPass = m_RenderPass;
			preDepthFramebufferSpec.Width = 1920;
			preDepthFramebufferSpec.Height = 1080;

			RenderTargetProperties rtProperties;
			rtProperties.DebugName = "Color Target";
			rtProperties.Format = ImageFormat::RGBA;
			rtProperties.ShaderResource = true;
			rtProperties.BaseMip = 0;
			rtProperties.MipCount = 1;
			rtProperties.BaseLayer = 0;
			rtProperties.LayerCount = 1;
			rtProperties.Resizable = true;
			rtProperties.Height = 1080;
			rtProperties.Width = 1920;

			preDepthFramebufferSpec.RenderTargetsInfo = { rtProperties };

			m_Framebuffer = ResourceManager::CreateFramebuffer(preDepthFramebufferSpec);
		}

		// Texture Test
		{
			TextureMetadata texMetadata;
			texMetadata.FilePath = "Resources/Textures/Ronaldo.jpg";

			m_TextureAsset = CreateScope<TextureAsset>(texMetadata);

			TextureProperties texProp;
			texProp.GenerateMips = true;
			m_TextureRonaldo = ResourceManager::CreateTexture2D(m_TextureAsset->GetFormat(), m_TextureAsset->GetWidth(),
				m_TextureAsset->GetHeight(), m_TextureAsset->GetWriteableBuffer().Data, texProp);

			Texture2D* texture = ResourceManager::GetTexture2D(m_TextureRonaldo);

			BindGroup* bindGroup = ResourceManager::GetBindGroup(m_RTBindGroup);
			bindGroup->SetImageView(texture->GetImageView(), 0);
		}



		UniformBufferSetSpecification uboSetSpec;
		uboSetSpec.Frames = ResourceManager::GetRendererConfig().FramesInFlight;
		uboSetSpec.Size = 100;
		m_UniformBufferSet = CreateRef<UniformBufferSet>(uboSetSpec);

		Renderer::ExecuteRenderQueue();
	}

	PathTracerSceneRenderer::~PathTracerSceneRenderer()
	{
		ResourceManager::DestroyTexture2D(m_TextureRonaldo);
		ResourceManager::DestroyFramebuffer(m_Framebuffer);
		ResourceManager::DestroyGraphicsPipeline(m_Pipeline);
		ResourceManager::DestroyRenderPass(m_RenderPass);
		ResourceManager::DestroyBindGroup(m_RTBindGroup);
		ResourceManager::DestroyBindGroupLayout(m_RTBindGroupLayout);
	}

	void PathTracerSceneRenderer::BeginScene(const SceneRendererCamera& sceneRendererCamera)
	{
	}

	void PathTracerSceneRenderer::EndScene()
	{
		/** /
		m_CommandBuffer->Begin();
		// Composite pass
		Renderer::BeginRenderPass(m_CommandBuffer, m_Pipeline->GetSpecification().RenderPass, true);
		{
			Renderer::RenderFullscreenQuad(m_CommandBuffer, m_Pipeline, m_UniformBufferSet, m_Material);
		}
		Renderer::EndRenderPass(m_CommandBuffer);
		m_CommandBuffer->End();
		m_CommandBuffer->Submit();
		/**/
	}

	void PathTracerSceneRenderer::FlushRenderPasses()
	{

	}

	void PathTracerSceneRenderer::SubmitMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4 & transform)
	{
	}

	void PathTracerSceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		Framebuffer* framebuffer = ResourceManager::GetFramebuffer(m_Framebuffer);
		framebuffer->Resize(width, height);
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
		}
		else
		{
			meshProxyHandle = m_MeshRegister.MeshSourceRegistered[meshSourceAssetHandle];
		}

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
		// Render Frame
		ResourceManager::ResetDescriptorPool();
		CommandBuffer* commnadBuffer = Renderer::GetMainCommandBuffer(m_Specification.SwapChainTarget);
		commnadBuffer->RT_Begin();
		{
			Renderer::BeginRenderPass(commnadBuffer, m_RenderPass, m_Framebuffer);
			Renderer::RenderFullscreenQuad(commnadBuffer, m_Pipeline, m_RTBindGroup);
			Renderer::EndRenderPass(commnadBuffer);
		}
		commnadBuffer->RT_End();
		commnadBuffer->RT_Submit();

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

	Handle<ImageView> PathTracerSceneRenderer::GetFinalPassImage() const
	{
		Framebuffer* framebuffer = ResourceManager::GetFramebuffer(m_Framebuffer);
		Handle<RenderTargetTexture> rtHandle = framebuffer->GetColorTarget(0);
		RenderTargetTexture* renderTarget = ResourceManager::GetRenderTarget(rtHandle);
		Handle<ImageView> imgViewHandle = renderTarget->GetImageView();
		return imgViewHandle;
	}
}

