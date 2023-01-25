#include "pch.h"
#include "ForwardSceneRenderer.h"
#include "Renderer/Renderer.h"

#include <Core/Application.h>
//#include "glm/gtx/string_cast.hpp"
#include "Utils/StringUtils.h"
#include "Core/Input.h"
#include "ImGui/ImGuiUI.h"

#if 0

namespace cerberus
{
	ForwardSceneRenderer::ForwardSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& sceneRendererSpec)
		: m_Scene(scene), m_Specification(sceneRendererSpec), m_ViewportWidth(m_Specification.Resolution.x), m_ViewportHeight(m_Specification.Resolution.y)
	{
		if (m_Specification.SwapChainTarget)
			m_CommandBuffer = CommandBuffer::CreateFromSwapchain();
		else
			m_CommandBuffer = CommandBuffer::Create(Renderer::GetRendererConfig().FramesInFlight, "SceneRenderer");


		// PreDepth
		{
			FramebufferSpecification preDepthFramebufferSpec;
			preDepthFramebufferSpec.AttachmentsSpecification = { ImageFormat::RED32F, ImageFormat::DEPTH24STENCIL8 };
			//preDepthFramebufferSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			preDepthFramebufferSpec.DebugName = "PreDepth";

			RenderPassSpecification preDepthRenderPassSpec;
			preDepthRenderPassSpec.TargetFramebuffer = CreateRef<Framebuffer>(preDepthFramebufferSpec);
			preDepthRenderPassSpec.DebugName = "PreDepth";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "PreDepth";
			Ref<Shader> shader = Renderer::GetShaderLibrary().Get("PreDepth");
			pipelineSpec.Shader = shader;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float3, "a_Normal" },
				{ ShaderDataType::Float3, "a_Tangent" },
				{ ShaderDataType::Float3, "a_Binormal" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};
			pipelineSpec.CullMode = CullMode::Back;
			pipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			pipelineSpec.RenderPass = CreateRef<RenderPass>(preDepthRenderPassSpec);
			m_PreDepthPipeline = CreateRef<Pipeline>(pipelineSpec);
			m_PreDepthMaterial = CreateRef<Material>(shader, "PreDepth");
		}
		// Shadow Map
		{
			uint32_t shadowMapRes = Renderer::GetRendererConfig().ShadowMapResolution;
			ImageSpecification imgSpec;
			imgSpec.Width = shadowMapRes;
			imgSpec.Height = shadowMapRes;
			imgSpec.Layers = 4;         // One layer per cascade
			imgSpec.Usage = ImageUsage::Attachment;
			imgSpec.DebugName = "ShadowCascadeImage";
			imgSpec.Format = ImageFormat::DEPTH32STENCIL8;
			Ref<Image2D> image = CreateRef<Image2D>(imgSpec);
			image->Invalidate();
			image->CreatePerLayerImageViews();

			FramebufferSpecification shadowFramebufferSpec;
			shadowFramebufferSpec.AttachmentsSpecification = { ImageFormat::DEPTH32STENCIL8 };
			shadowFramebufferSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			//shadowFramebufferSpec.DepthClearValue = 1.0f;
			shadowFramebufferSpec.NoResize = true;
			shadowFramebufferSpec.DebugName = "ShadowMapCascade";
			shadowFramebufferSpec.ExistingImage = image;
			shadowFramebufferSpec.Width = shadowMapRes;
			shadowFramebufferSpec.Height = shadowMapRes;

			// 4 cascades
			auto shadowPassShader = Renderer::GetShaderLibrary().Get("ShadowMap");
			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "ShadowMapCascade";
			pipelineSpec.Shader = shadowPassShader;
			pipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			pipelineSpec.CullMode = CullMode::Back;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float3, "a_Normal" },
				{ ShaderDataType::Float3, "a_Tangent" },
				{ ShaderDataType::Float3, "a_Binormal" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			for (uint32_t i = 0; i < 4; i++)
			{
				// Defining which layer (cascade of the shadow map) is going to be attach to each pipeline
				shadowFramebufferSpec.ExistingImageLayers.clear();
				shadowFramebufferSpec.ExistingImageLayers.emplace_back(i);

				RenderPassSpecification shadowMapRenderPassSpec;
				shadowMapRenderPassSpec.TargetFramebuffer = CreateRef<Framebuffer>(shadowFramebufferSpec);
				shadowMapRenderPassSpec.DebugName = "ShadowMapCascade";
				pipelineSpec.RenderPass = CreateRef<RenderPass>(shadowMapRenderPassSpec);
				m_ShadowMapPipelines[i] = CreateRef<Pipeline>(pipelineSpec);

			}
			Ref<Shader> shader = Renderer::GetShaderLibrary().Get("ShadowMap");
			m_ShadowMapMaterial = CreateRef<Material>(shader, "ShadowMap");

		}
		// Geometry pass
		{
			// Framebuffer
			FramebufferSpecification framebufferSpec;
			framebufferSpec.SwapchainTarget = false;
			framebufferSpec.DebugName = "Geometry";
			framebufferSpec.AttachmentsSpecification = { ImageFormat::RGBA32F, ImageFormat::DEPTH24STENCIL8 };
			framebufferSpec.ClearDepthOnLoad = false;
			framebufferSpec.ExistingImages[1] = m_PreDepthPipeline->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->GetDepthImage();
			Ref<Framebuffer> framebuffer = CreateRef<Framebuffer>(framebufferSpec);
			// Render Pass
			RenderPassSpecification renderPassSpec;
			renderPassSpec.TargetFramebuffer = framebuffer;
			renderPassSpec.DebugName = "Geometry Pass";
			Ref<RenderPass> renderPass = CreateRef<RenderPass>(renderPassSpec);
			// Pipeline
			Ref<Shader> shader = Renderer::GetShaderLibrary().Get("PBR");
			PipelineSpecification pipelineSpec;
			pipelineSpec.Layout = { { ShaderDataType::Float3, "a_Position"	},
									{ ShaderDataType::Float3, "a_Normal"	},
									{ ShaderDataType::Float3, "a_Tangent"	},
									{ ShaderDataType::Float3, "a_Binormal"	},
									{ ShaderDataType::Float2, "a_TexCoord"	}, };
			pipelineSpec.Shader = shader;
			pipelineSpec.DepthWrite = false;
			pipelineSpec.DepthOperator = DepthCompareOperator::Equal;
			pipelineSpec.RenderPass = renderPass;
			pipelineSpec.CullMode = CullMode::Back;
			pipelineSpec.DebugName = "Geometry Pass";
			m_GeometryPipeline = CreateRef<Pipeline>(pipelineSpec);
		}
		// Skybox
		{
			// Material
			Ref<Shader> skyboxShader = Renderer::GetShaderLibrary().Get("Skybox");
			m_SkyboxMaterial = CreateRef<Material>(skyboxShader);
			// Pipeline
			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "Skybox";
			pipelineSpec.Shader = skyboxShader;
			// -- Full screen quad
			pipelineSpec.Layout = {  };
#if VK_INVERTED_UP_COORD_SYSTEM
			pipelineSpec.CullMode = CullMode::Front;
#else
			pipelineSpec.CullMode = CullMode::Back;
#endif
			pipelineSpec.DepthTest = false;
			pipelineSpec.DepthWrite = false;
			// -- Render to the same target texture as the geometry pass
			pipelineSpec.RenderPass = m_GeometryPipeline->GetSpecification().RenderPass;
			m_SkyboxPipeline = CreateRef<Pipeline>(pipelineSpec);

		}
		// Scene Composite pass
		{
			// Framebuffer
			FramebufferSpecification framebufferSpec;
			framebufferSpec.SwapchainTarget = m_Specification.SwapChainTarget;
			framebufferSpec.DebugName = "Composite";
			framebufferSpec.AttachmentsSpecification = { ImageFormat::RGBA };
			Ref<Framebuffer> framebuffer = CreateRef<Framebuffer>(framebufferSpec);
			// Render Pass
			RenderPassSpecification renderPassSpec;
			renderPassSpec.TargetFramebuffer = framebuffer;
			renderPassSpec.DebugName = "Scene Composite Pass";
			Ref<RenderPass> renderPass = CreateRef<RenderPass>(renderPassSpec);
			// Material
			Ref<Shader> sceneCompositeShader = Renderer::GetShaderLibrary().Get("SceneComposite");
			m_SceneCompositeMaterial = CreateRef<Material>(sceneCompositeShader, "Scene Composite Material", false);
			// Pipeline
			PipelineSpecification pipelineSpec;
			pipelineSpec.Layout = {  }; // Full Screen Quad (Positions ans Tex Coord are created inside the vertex shader)
#if VK_INVERTED_UP_COORD_SYSTEM
			pipelineSpec.CullMode = CullMode::Front;
#else
			pipelineSpec.CullMode = CullMode::Back;
#endif
			pipelineSpec.Shader = sceneCompositeShader;
			pipelineSpec.RenderPass = renderPass;
			pipelineSpec.DebugName = "Scene Composite";
			pipelineSpec.DepthTest = false;
			pipelineSpec.DepthWrite = false;
			m_SceneCompositePipeline = CreateRef<Pipeline>(pipelineSpec);

		}
		// UniformBuffer;
		m_UniformBufferSet = CreateRef<UniformBufferSet>(Renderer::GetRendererConfig().FramesInFlight);
		m_UniformBufferSet->Create(sizeof(CameraUB), 0);
		m_UniformBufferSet->Create(sizeof(EnvironmentUB), 1);
		m_UniformBufferSet->Create(sizeof(RendererDataUB), 2);
		m_UniformBufferSet->Create(sizeof(ShadowDataUB), 3);
		m_UniformBufferSet->Create(sizeof(ScreenDataUB), 17);

	}
	ForwardSceneRenderer::~ForwardSceneRenderer()
	{
	}

	void ForwardSceneRenderer::BeginScene(const SceneRendererCamera& sceneRendererCamera)
	{
		/////////////////////////////////////////////////////
		///  Update Scene Data
		/////////////////////////////////////////////////////
		m_SceneData.SceneRendererCamera = sceneRendererCamera;
		m_SceneData.LightEnvironment = m_Scene->m_LightEnvironment;
		/////////////////////////////////////////////////////
		///  Resize
		/////////////////////////////////////////////////////
		if (m_NeedsResize)
		{
			m_NeedsResize = false;

		}
		/////////////////////////////////////////////////////
		///  Update Uniform Buffer Data (UBO)
		/////////////////////////////////////////////////////
		uint32_t frameInFlight = Renderer::GetCurrentFrameIndex();

		// Camera
		Ref<UniformBuffer> cameraUniformBuffer = m_UniformBufferSet->Get(0, 0, frameInFlight);
		CameraUB cameraUBO;
		cameraUBO.Projection = m_SceneData.SceneRendererCamera.Camera.GetProjection();
		cameraUBO.View = m_SceneData.SceneRendererCamera.ViewMatrix;
		cameraUBO.ViewProjection = cameraUBO.Projection * cameraUBO.View;
		cameraUBO.InverseView = glm::inverse(cameraUBO.View);
		cameraUBO.InverseProjection = glm::inverse(cameraUBO.Projection);
		glm::mat4 inverseViewProjection = cameraUBO.InverseView * cameraUBO.InverseProjection;
		cameraUBO.InverseViewReverseProjection = cameraUBO.InverseView * glm::inverse(m_SceneData.SceneRendererCamera.Camera.GetReverseProjection());
		cameraUniformBuffer->SetData(&cameraUBO, sizeof(CameraUB), 0);
		// Light Environment
		{
			Ref<UniformBuffer> environmentUniformBuffer = m_UniformBufferSet->Get(1, 0, frameInFlight);
			EnvironmentUB environmentUB;
			for (uint32_t i = 0; i < (uint32_t)environmentUB.DirectionalLights.size(); i++)
			{
				auto& sceneLight = m_SceneData.LightEnvironment.DirectionalLights[i];
				environmentUB.DirectionalLights[i].Direction = { sceneLight.Direction, 1.f };
				environmentUB.DirectionalLights[i].Intensity = sceneLight.Multiplier;
				environmentUB.DirectionalLights[i].Radience = { sceneLight.Radiance, 1.f };
			}
			environmentUB.CameraPosition = sceneRendererCamera.ViewPosition;
			environmentUniformBuffer->SetData(&environmentUB, sizeof(EnvironmentUB), 0);
		}
		// Screen Data
		{
			Ref<UniformBuffer> screenUniformBuffer = m_UniformBufferSet->Get(17, 0, frameInFlight);
			ScreenDataUB screenUB;
			screenUB.Resolution = { m_ViewportWidth , m_ViewportHeight };
			screenUniformBuffer->SetData(&screenUB, sizeof(ScreenDataUB), 0);
		}
		// Shadow Data
		{
			m_ShadowData.UpdateShadowCascade(m_SceneData.SceneRendererCamera, inverseViewProjection, m_SceneData.LightEnvironment.DirectionalLights[0].Direction);
			ShadowDataUB shadowUB;
			uint32_t shadowCascades = Renderer::GetRendererConfig().ShadowCascadesCount;
			for (uint32_t i = 0; i < shadowCascades; i++)
				shadowUB.LightViewProjectionMatrix[i] = m_ShadowData.GetShadowCascade(i).ViewProjectionMatrix;

			Ref<UniformBuffer> shadowUniformBuffer = m_UniformBufferSet->Get(3, 0, frameInFlight);
			shadowUniformBuffer->SetData(&shadowUB, sizeof(ShadowDataUB), 0);
		}
		// Renderer Data
		{
			uint32_t shadowCascades = Renderer::GetRendererConfig().ShadowCascadesCount;
			for (uint32_t i = 0; i < shadowCascades; i++)
				m_RendererDataUB.CascadeSplits[i] = m_ShadowData.GetShadowCascade(i).CascadeSplit;

			Ref<UniformBuffer> rendererUniformBuffer = m_UniformBufferSet->Get(2, 0, frameInFlight);
			rendererUniformBuffer->SetData(&m_RendererDataUB, sizeof(RendererDataUB), 0);
		}
		/////////////////////////////////////////////////////
		///  Update Environment
		/////////////////////////////////////////////////////
		Ref<Image2D> shadowMap = m_ShadowMapPipelines[0]->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->GetDepthImage();
		Renderer::SetSceneEnvironment(this, m_SceneData.LightEnvironment.SkyLight.EnvironmentMap, shadowMap);
	}
	void ForwardSceneRenderer::EndScene()
	{
		FlushAndReset();
	}

	void ForwardSceneRenderer::SubmitMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform)
	{
		m_SceneData.DrawList.push_back({ mesh, materialTable, transform });
	}

	void ForwardSceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (m_ViewportWidth != width || m_ViewportHeight != height)
		{
			m_ViewportWidth = width;
			m_ViewportHeight = height;
			m_NeedsResize = true;
			m_PreDepthPipeline->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPipeline->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SceneCompositePipeline->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SkyboxPipeline->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
		}
	}

	void ForwardSceneRenderer::OnImGuiRender()
	{
		ImGui::Begin("Renderer");
		{
			if (ui::PropertyGridHeader("Statistics", true))
			{
				ImGui::Separator();
				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
				GPUMemoryStats memoryStats = ResourceAllocator::GetStats();
				std::string used = utils::BytesToString(memoryStats.Used);
				std::string free = utils::BytesToString(memoryStats.Free);
				ImGui::Text("Used VRAM: %s", used.c_str());
				ImGui::Text("Free VRAM: %s", free.c_str());

				if (ui::PropertyGridHeader("Render Pass Statistics", true))
				{
					ImGui::Text("GPU time per frame: %.3fms", m_GPUStatistics.GPUTimePerFrame);
					ImGui::Text("Depth Pre Pass: %.3fms", m_GPUStatistics.DepthPrePass.TimeStamp);
					ImGui::Text("Shadow Map Pass: %.3fms", m_GPUStatistics.ShadowPass.TimeStamp);
					ImGui::Text("Geometry Pass: %.3fms", m_GPUStatistics.GeometryPass.TimeStamp);
					ImGui::Text("Scene Composite Pass: %.3fms", m_GPUStatistics.SceneCompositePass.TimeStamp);

					ui::EndTreeNode();

				}

				if (ui::BeginTreeNode("Pipeline Statistics"))
				{
					const auto& pipelineStats = m_CommandBuffer->GetPipelineStatistics();
					ImGui::Text("Input Assembly Vertices: %llu", pipelineStats.InputAssemblyVertices);
					ImGui::Text("Input Assembly Primitives: %llu", pipelineStats.InputAssemblyPrimitives);
					ImGui::Text("Vertex Shader Invocations: %llu", pipelineStats.VertexShaderInvocations);
					ImGui::Text("Clipping Invocations: %llu", pipelineStats.ClippingInvocations);
					ImGui::Text("Clipping Primitives: %llu", pipelineStats.ClippingPrimitives);
					ImGui::Text("Fragment Shader Invocations: %llu", pipelineStats.FragmentShaderInvocations);
					ImGui::Text("Compute Shader Invocations: %llu", pipelineStats.ComputeShaderInvocations);
					ui::EndTreeNode();
				}
				ui::EndTreeNode();

			}
			if (ui::PropertyGridHeader("AA Techniques", true))
			{
				ui::BeginPropertyGrid();
				ui::Property("Enable FXAA", m_EnableFXAA);
				ui::EndPropertyGrid();
				ui::EndTreeNode();
			}

			if (ui::PropertyGridHeader("Shadows", true))
			{
				ui::BeginPropertyGrid();
				ui::Property("Show Cascades", m_RendererDataUB.ShowCascades);
				ui::Property("Soft Shadows", m_RendererDataUB.SoftShadows);
				ui::Property("Directional Light Size", m_RendererDataUB.LightSize, 0.01f);
				ui::Property("Max Shadow Distance", m_RendererDataUB.MaxShadowDistance, 1.0f);
				ui::Property("Shadow Fade", m_RendererDataUB.ShadowFade, 5.0f);
				ui::EndPropertyGrid();
				if (ui::BeginTreeNode("Cascade Settings"))
				{
					ui::BeginPropertyGrid();
					ui::Property("Cascade Fading", m_RendererDataUB.CascadeFading);
					ui::Property("Cascade Transition Fade", m_RendererDataUB.CascadeTransitionFade, 0.05f, 0.0f, FLT_MAX);
					ui::Property("Cascade Split", *m_ShadowData.GetCascadeSplitLambda(), 0.01f);
					ui::Property("Cascade Near Plane Offset", *m_ShadowData.GetCascadeNearPlaneOffset(), 0.1f, -FLT_MAX, 0.0f);
					ui::Property("Cascade Far Plane Offset", *m_ShadowData.GetCascadeFarPlaneOffset(), 0.1f, 0.0f, FLT_MAX);
					ui::EndPropertyGrid();
					ui::EndTreeNode();
				}
				if (ui::BeginTreeNode("Shadow Maps", false))
				{
					Ref<Image2D> image = m_ShadowMapPipelines[0]->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->GetDepthImage();
					float size = ImGui::GetContentRegionAvailWidth();
					ui::BeginPropertyGrid();
					ui::PropertySlider("Cascade Index", m_CascadeIndex, 0, 3);
					ui::EndPropertyGrid();
					ui::Image(image, (uint32_t)m_CascadeIndex, { size, size }, { 0, 1 }, { 1, 0 });
					ui::EndTreeNode();
				}
				ui::EndTreeNode();
			}
		}
		ImGui::End();
		/**/
	}
	Ref<Image2D> ForwardSceneRenderer::GetFinalPassImage() const
	{
		return m_SceneCompositePipeline->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->GetImage(0);
	}
	void ForwardSceneRenderer::FlushAndReset()
	{
		m_CommandBuffer->Begin();
		{
			ShadowPass();
			PreDepthPass();
			GeometryPass();
			SceneCompositePass();
		}
		m_CommandBuffer->End();
		m_CommandBuffer->Submit();

		m_GPUStatistics.GPUTimePerFrame = m_CommandBuffer->GetExecutionGPUTime(0);
		m_GPUStatistics.DepthPrePass.TimeStamp = m_CommandBuffer->GetExecutionGPUTime(m_GPUStatistics.DepthPrePass.QueryIndex);
		m_GPUStatistics.ShadowPass.TimeStamp = m_CommandBuffer->GetExecutionGPUTime(m_GPUStatistics.ShadowPass.QueryIndex);
		m_GPUStatistics.GeometryPass.TimeStamp = m_CommandBuffer->GetExecutionGPUTime(m_GPUStatistics.GeometryPass.QueryIndex);
		m_GPUStatistics.SceneCompositePass.TimeStamp = m_CommandBuffer->GetExecutionGPUTime(m_GPUStatistics.SceneCompositePass.QueryIndex);

		// Clear Data
		m_SceneData.DrawList.clear();
		m_SceneData = {};
	}

	void ForwardSceneRenderer::PreDepthPass()
	{
		uint32_t queryIndex = m_CommandBuffer->BeginTimestampQuery();
		m_GPUStatistics.DepthPrePass.QueryIndex = queryIndex;
		Renderer::BeginRenderPass(m_CommandBuffer, m_PreDepthPipeline->GetSpecification().RenderPass);
		{
			for (auto& mesh : m_SceneData.DrawList)
				Renderer::RenderMeshWithMaterial(m_CommandBuffer, m_UniformBufferSet, m_PreDepthPipeline, mesh.Mesh, mesh.Transform, m_PreDepthMaterial);
		}
		Renderer::EndRenderPass(m_CommandBuffer);
		m_CommandBuffer->EndTimestampQuery(queryIndex);
	}
	void ForwardSceneRenderer::ShadowPass()
	{
		uint32_t queryIndex = m_CommandBuffer->BeginTimestampQuery();
		m_GPUStatistics.ShadowPass.QueryIndex = queryIndex;
		for (int i = 0; i < 4; i++)
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_ShadowMapPipelines[i]->GetSpecification().RenderPass);
			// Render entities
			Buffer cascade(&i, sizeof(uint32_t));
			for (auto& dc : m_SceneData.DrawList)
			{
				Renderer::RenderMeshWithMaterial(m_CommandBuffer, m_UniformBufferSet, m_ShadowMapPipelines[i], dc.Mesh, dc.Transform, m_ShadowMapMaterial, cascade);
			}
			Renderer::EndRenderPass(m_CommandBuffer);
		}

		m_CommandBuffer->EndTimestampQuery(queryIndex);
	}
	void ForwardSceneRenderer::GeometryPass()
	{
		uint32_t queryIndex = m_CommandBuffer->BeginTimestampQuery();
		m_GPUStatistics.GeometryPass.QueryIndex = queryIndex;
		Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryPipeline->GetSpecification().RenderPass);
		{
			// Render Skybox
			m_SkyboxMaterial->Set("u_MaterialUniforms.TextureLod", m_SceneData.LightEnvironment.SkyLight.SampleLOD);
			m_SkyboxMaterial->Set("u_MaterialUniforms.Intensity", m_SceneData.LightEnvironment.SkyLight.Multiplier);
			const Ref<TextureCube> radianceMap = m_SceneData.LightEnvironment.SkyLight.EnvironmentMap->RadianceMap;
			m_SkyboxMaterial->Set("u_Texture", radianceMap);
			Renderer::RenderFullscreenQuad(m_CommandBuffer, m_SkyboxPipeline, m_UniformBufferSet, m_SkyboxMaterial);
			// Render Geometry
			/**/
			for (auto& mesh : m_SceneData.DrawList)
				Renderer::RenderMesh(m_CommandBuffer, m_UniformBufferSet, m_GeometryPipeline, mesh.Mesh, mesh.MaterialTable, mesh.Transform);
			/**/
		}
		Renderer::EndRenderPass(m_CommandBuffer);
		m_CommandBuffer->EndTimestampQuery(queryIndex);
	}
	void ForwardSceneRenderer::SceneCompositePass()
	{
		uint32_t queryIndex = m_CommandBuffer->BeginTimestampQuery();
		m_GPUStatistics.SceneCompositePass.QueryIndex = queryIndex;
		// Update Scene Composite Material
		float exposure = m_SceneData.SceneRendererCamera.Camera.GetExposure();
		Ref<Image2D> sceneImage = m_GeometryPipeline->GetSpecification().RenderPass->GetSpecification().TargetFramebuffer->GetImage(0);
		m_SceneCompositeMaterial->Set("u_Uniforms.Exposure", exposure);
		m_SceneCompositeMaterial->Set("u_Uniforms.FXAA", m_EnableFXAA);
		m_SceneCompositeMaterial->Set("u_Texture", sceneImage);
		// Composite pass
		Renderer::BeginRenderPass(m_CommandBuffer, m_SceneCompositePipeline->GetSpecification().RenderPass, true);
		{
			Renderer::RenderFullscreenQuad(m_CommandBuffer, m_SceneCompositePipeline, m_UniformBufferSet, m_SceneCompositeMaterial);
		}
		Renderer::EndRenderPass(m_CommandBuffer);
		m_CommandBuffer->EndTimestampQuery(queryIndex);
	}
}
#endif // 0
