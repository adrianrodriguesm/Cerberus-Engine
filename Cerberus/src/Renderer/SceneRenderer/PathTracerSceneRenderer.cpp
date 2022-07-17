#include "pch.h"
#include "PathTracerSceneRenderer.h"
#include "Renderer/Renderer.h"
#include "Asset/AssetManager.h"
namespace cerberus
{

	PathTracerSceneRenderer::PathTracerSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec)
	{

		if (m_Specification.SwapChainTarget)
			m_CommandBuffer = RenderCommandBuffer::CreateFromSwapchain();
		else
			m_CommandBuffer = RenderCommandBuffer::Create(Renderer::GetRendererConfig().FramesInFlight, "SceneRenderer");


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

		AssetHandle cubeHandle = AssetManager::GetAssetHandleFromFilePath(AssetManager::GetAssetPath().string() + "/Meshes/Default/Sphere.cmesh");
		m_Cube = AssetManager::GetAsset<Mesh>(cubeHandle);
		m_ASBuilder = CreateScope<AccelerationStructureBuilder>();
		Renderer::ExecuteRenderQueue();
		m_ASBuilder->BeginBLASBuild(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
		m_ASBuilder->SummitMesh(m_Cube);
		m_ASBuilder->EndBLASBuild();
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
		// Composite pass
		Renderer::BeginRenderPass(m_CommandBuffer, m_Pipeline->GetSpecification().RenderPass, true);
		{
			Renderer::RenderFullscreenQuad(m_CommandBuffer, m_Pipeline, m_UniformBufferSet, m_Material);
		}
		Renderer::EndRenderPass(m_CommandBuffer);
	}

	void PathTracerSceneRenderer::SubmitMesh(const Ref<Mesh>&mesh, const Ref<MaterialTable>&materialTable, const glm::mat4 & transform)
	{
	}

	void PathTracerSceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
	}

	void PathTracerSceneRenderer::OnImGuiRender()
	{
	}

	Ref<Image2D> PathTracerSceneRenderer::GetFinalPassImage() const
	{
		return m_Pipeline->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->GetImage(0);;
	}
}

