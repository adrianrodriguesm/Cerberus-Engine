#pragma once
#include "Renderer/CommandBuffer.h"
#include "Renderer/Pipeline.h"
#include "Scene/Scene.h"
#include "Renderer/UniformBufferSet.h"
#include "Renderer/Mesh.h"
#include "Renderer/Camera.h"
#include "Renderer/ComputePipeline.h"
#include "Renderer/Shadow.h"
#include "Renderer/Image.h"
namespace cerberus
{
	struct SceneRendererSpecification
	{
		bool SwapChainTarget = false;
		glm::ivec2 Resolution;
	};

	struct SceneRendererCamera
	{
		Camera Camera;
		glm::vec3 ViewPosition;
		glm::vec3 Target;
		glm::mat4 ViewMatrix;
		float Near, Far;
	};

	class SceneRenderer
	{
	public:
		SceneRenderer() = default;
		virtual ~SceneRenderer() = default;

		virtual void BeginScene(const SceneRendererCamera& sceneRendererCamera) = 0;
		virtual void EndScene() = 0;
		virtual void SubmitMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform) = 0;
		virtual void SetViewportSize(uint32_t width, uint32_t height) = 0;
		virtual void SetSceneContext(const Ref<Scene>& scene) = 0;

		virtual bool IsRegistered(const Ref<Mesh>& mesh) = 0;
		virtual bool RegisterMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform) { return false; }
		virtual bool UnregisterMesh(const Ref<Mesh>& mesh) { return false; }
		virtual void UpdateMeshTransform(const Ref<Mesh>& mesh, const glm::mat4& transform) {}
		virtual bool UpdateMeshMaterials(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable) { return false; }
		virtual void OnMaterialAssetChanged(const Ref<MaterialAsset>& materialAsset) {}
		virtual void SetSkyLight(const Ref<EnvironmentMap>& environment, float intensity, uint32_t sampleLOD) {}

		virtual void Render(const SceneRendererCamera& sceneRendererCamera) = 0;
		
		virtual void OnImGuiRender() = 0;

		virtual Handle<ImageView> GetFinalPassImage() const = 0;


		
	};
}
