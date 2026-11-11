#pragma once
#include "Scene/Scene.h"
#include "Asset/Assets/MeshAsset.h"
#include "Renderer/Scene/Camera.h"
#include "Renderer/RHI/Image.h"
#include "Renderer/Core/RenderObjectID.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include <span>
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

		virtual void SetViewportSize(uint32_t width, uint32_t height) = 0;

		virtual bool IsRegistered(RenderObjectID renderObjectID) = 0;
		// submeshIndex: -1 registers every submesh (default). >= 0 filters
		// registration down to that single submesh index.
		virtual bool RegisterMesh(RenderObjectID renderObjectID, const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform, int32_t submeshIndex = -1) = 0;
		virtual bool UnregisterMesh(RenderObjectID renderObjectID) = 0;
		virtual void UpdateMeshTransform(RenderObjectID renderObjectID, const glm::mat4& transform) = 0;
		virtual bool UpdateMeshMaterials(RenderObjectID renderObjectID, const Ref<MaterialTable>& materialTable) = 0;
		virtual void OnMaterialAssetChanged(const Ref<MaterialAsset>& materialAsset) = 0;
		virtual void SetSkyLight(const Ref<EnvironmentMap>& environment, float intensity, uint32_t sampleLOD) = 0;
		virtual void SetDirectionalLights(std::span<const DirectionalLight> directionalLights) = 0;

		virtual void Render(const SceneRendererCamera& sceneRendererCamera) = 0;
		
		virtual void OnImGuiRender() = 0;

		virtual Handle<ImageView> GetFinalPassImage() const = 0;

		// Per-pass GPU times from this renderer's RenderGraph for the last
		// Execute()'d frame -- same accessor and one-submission-cycle lag as
		// RenderGraph::GetPassTimings(). Exists so callers that only hold a
		// SceneRenderer& (e.g. a benchmark capture) don't need to know whether
		// the active renderer is Forward or PathTracer. Empty if this renderer
		// hasn't rendered a frame yet.
		virtual std::span<const RGPassTiming> GetLastFramePassTimings() const = 0;

		// Scene geometric complexity, for reporting (e.g. a benchmark CSV
		// header) -- see RenderScene::GetTotalTriangleCount()'s comment for
		// exactly what this counts.
		virtual uint32_t GetTotalTriangleCount() const = 0;
		virtual uint32_t GetMeshInstanceCount() const = 0;

	};
}
