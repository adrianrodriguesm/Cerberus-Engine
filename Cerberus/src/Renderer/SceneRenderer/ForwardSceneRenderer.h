#pragma once
#include "Renderer/SceneRenderer/SceneRenderer.h"

namespace cerberus
{
	class ForwardSceneRenderer final: public SceneRenderer
	{
	public:
		ForwardSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec = {});
		virtual ~ForwardSceneRenderer();

		virtual void BeginScene(const SceneRendererCamera& sceneRendererCamera) override;
		virtual void EndScene() override;
		virtual void SubmitMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform) override;
		virtual void SetViewportSize(uint32_t width, uint32_t height) override;
		virtual void SetSceneContext(const Ref<Scene>& scene) override { m_Scene = scene; }

		virtual void Render(const SceneRendererCamera& sceneRendererCamera) override {}

		virtual void OnImGuiRender() override;

		virtual Ref<Image2D> GetFinalPassImage() const override;
	private:
		void FlushAndReset();
		void PreDepthPass();
		void ShadowPass();
		void GeometryPass();
		void SceneCompositePass();
	private:
		struct CameraUB
		{
			glm::mat4 ViewProjection, View, Projection;
			glm::mat4 InverseViewReverseProjection, InverseView, InverseProjection;
		};
		struct DirectionalLightUB
		{
			glm::vec4 Direction;
			glm::vec4 Radience;
			float Intensity = 1;
			float Padding[3] = { 0,0,0 };
		};
		struct EnvironmentUB
		{
			std::array<DirectionalLightUB, 4> DirectionalLights;
			glm::vec3 CameraPosition;
			float Padding = 0.f;
		};
		struct ScreenDataUB
		{
			glm::ivec2 Resolution;
		};
		struct ShadowDataUB
		{
			std::array<glm::mat4, 4> LightViewProjectionMatrix;
		};
		struct RendererDataUB
		{
			glm::vec4 CascadeSplits;
			bool ShowCascades = false;				// Bool are 4 byte in glsl
			char Padding_0[3] = { 0,0,0 };
			bool SoftShadows = true;
			char Padding_1[3] = { 0,0,0 };
			float LightSize = .17f;
			float MaxShadowDistance = 500.f;
			float ShadowFade = 1.f;
			bool CascadeFading = true;
			char Padding_2[3] = { 0,0,0 };
			float CascadeTransitionFade = 1.f;
		} m_RendererDataUB;


		struct RenderPassStatistics
		{
			uint32_t QueryIndex;
			float TimeStamp; // in Milliseconds
		};

		struct GPUStatistics
		{
			float GPUTimePerFrame = 0.f;
			RenderPassStatistics DepthPrePass;
			RenderPassStatistics GeometryPass;
			RenderPassStatistics ShadowPass;
			RenderPassStatistics SceneCompositePass;
		} m_GPUStatistics;

	private:
		Ref<Scene> m_Scene;
		SceneRendererSpecification m_Specification;
		Ref<CommandBuffer> m_CommandBuffer;
		// Pipelines
		Ref<Pipeline> m_PreDepthPipeline;
		Ref<Pipeline> m_SkyboxPipeline;
		Ref<Pipeline> m_GeometryPipeline;
		Ref<Pipeline> m_SceneCompositePipeline;
		Ref<Pipeline> m_ShadowMapPipelines[4];

		// Material
		Ref<Material> m_PreDepthMaterial;
		Ref<Material> m_SkyboxMaterial;
		Ref<Material> m_SceneCompositeMaterial;
		Ref<Material> m_ShadowMapMaterial;

		Ref<UniformBufferSet> m_UniformBufferSet;

		// Viewport
		uint32_t m_ViewportWidth, m_ViewportHeight;
		bool m_NeedsResize = false;
		bool m_EnableFXAA = true;

		Shadow m_ShadowData;
		int m_CascadeIndex = 0;
		bool m_DebugShadowMap = false;

		struct DrawableEntity
		{
			Ref<Mesh> Mesh;
			Ref<MaterialTable> MaterialTable;
			glm::mat4 Transform;
		};

		struct SceneData
		{
			SceneRendererCamera SceneRendererCamera;
			std::vector<DrawableEntity> DrawList;
			LightEnvironment LightEnvironment;
		} m_SceneData;

	};
}