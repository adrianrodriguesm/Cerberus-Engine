#pragma once
#include "Renderer/SceneRenderer/SceneRenderer.h"
#include "Scene/Scene.h"
#include "Renderer/AccelerationStructure.h"
namespace cerberus
{
	class PathTracerSceneRenderer final : public SceneRenderer
	{
	public:
		PathTracerSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec = {});
		virtual ~PathTracerSceneRenderer();

		virtual void BeginScene(const SceneRendererCamera& sceneRendererCamera) override;
		virtual void EndScene() override;
		virtual void SubmitMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform) override;
		virtual void SetViewportSize(uint32_t width, uint32_t height) override;
		virtual void SetSceneContext(const Ref<Scene>& scene) override { m_Scene = scene; }

		virtual void OnImGuiRender() override;

		virtual Ref<Image2D> GetFinalPassImage() const override;
	private:
		SceneRendererSpecification m_Specification;
		Ref<RenderCommandBuffer> m_CommandBuffer;

		Ref<Scene> m_Scene;
		
		Ref<Image2D> m_FinalPassImage;

		Scope<AccelerationStructureBuilder> m_ASBuilder;
		Ref<Mesh> m_Cube;

		Ref<Pipeline> m_Pipeline;
		Ref<Material> m_Material;

		Ref<UniformBufferSet> m_UniformBufferSet;
	};
}