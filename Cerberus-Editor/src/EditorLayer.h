#pragma once
#include "Cerberus.h"

#include "Panels/EditorPanel.h"
#include "ImGui/ImGuizmo.h"
#include <limits>
#include <unordered_map>
namespace cerberus
{
	class EditorLayer : public Layer
	{
		struct ViewportRay
		{
			glm::vec3 Origin = glm::vec3(0.0f);
			glm::vec3 Direction = glm::vec3(0.0f, 0.0f, -1.0f);
		};

		struct ViewportHit
		{
			Entity HitEntity;
			glm::vec3 Position = glm::vec3(0.0f);
			float Distance = std::numeric_limits<float>::max();
			uint32_t MaterialSlot = 0;
		};

		struct MeshBounds
		{
			glm::vec3 Min = glm::vec3(0.0f);
			glm::vec3 Max = glm::vec3(0.0f);
			bool Valid = false;
		};

	public:
		EditorLayer();
		~EditorLayer() = default;
		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnImGuiRender() override;
		virtual void OnFrameEnd() override;
		virtual void OnEvent(Event& event) override;
		virtual void OnUpdate(Timestep ts) override;

		void SelectEntities(const std::vector<Entity>& entities);
		// Convenience wrapper for the many call sites that only ever select one
		// entity at a time.
		void SelectEntity(const Entity& entity) { SelectEntities({ entity }); }
		void DuplicateSelectedEntity();
		void DeleteSelectedEntities();
		void FocusEntity(Entity entity);

		void NewScene();
		void OpenScene();
		void OpenScene(const Ref<Scene>& scene);
		void SaveSceneAs();
		void SaveScene();
		// Restore / capture the viewport camera pose persisted in the scene file.
		void ApplyEditorCameraFromScene();
		void StoreEditorCameraToScene();
		// Events
		bool OnWindowResize(WindowResizeEvent& event);
		bool OnKeyPress(KeyPressedEvent& event);

		void UI_RenderTittleBar();
		void UI_Viewport();
		void UI_DockSpace();

	private:
		bool BuildViewportRay(const ImVec2& viewportPosition, const ImVec2& viewportSize, ViewportRay& ray) const;
		bool RaycastScene(const ViewportRay& ray, ViewportHit& closestHit);
		MeshBounds GetMeshBounds(const Ref<MeshSource>& meshSource);
		// AABB center of what the gizmo should be anchored to for this entity,
		// in the entity's own local space: the whole mesh's bounds for a
		// normal MeshComponent, or just that one submesh's raw (un-baked)
		// bounds for a split entity -- see GetEntityGizmoPivotOffset's body
		// for why those two cases need different bounds. Zero for anything
		// without a MeshComponent.
		glm::vec3 GetEntityGizmoPivotOffset(Entity entity);
		AssetHandle ResolveDroppedMesh(AssetHandle assetHandle);
		glm::vec3 ComputeDropPlacement(const ViewportRay& ray, const ViewportHit* surfaceHit) const;
		Entity CreateMeshEntityFromDrop(AssetHandle meshHandle, const glm::vec3& placement, Entity parent = {});
		Entity CreateSplitMeshEntitiesFromDrop(AssetHandle meshHandle, const glm::vec3& placement, Entity parent = {});
		void RenderSubmeshSplitPopup();
		// Dropping a mesh asset onto an existing Scene Hierarchy node instead of
		// the viewport -- same resolve/split-popup flow, but parented under the
		// dropped-on entity at identity local transform instead of root-level at
		// a ray/hit position.
		void OnAssetDroppedOnEntity(AssetHandle assetHandle, Entity parent);
		bool ApplyMaterialFromDrop(AssetHandle materialHandle, const ViewportHit& hit);
		void RequestSceneRenderer(SceneRendererType rendererType);
		void RecreateSceneRenderer();

		Ref<Scene> m_Scene;
		std::vector<Entity> m_SelectedEntities;

		int m_GizmoType = -1;
		bool m_ViewportPanelMouseOver = false, m_ViewportPanelFocused = false;
		bool m_IsViewportMaximized = false;

		std::function<void(const std::vector<Entity>&)> m_SelectionChangedCallback = [&](const std::vector<Entity>& entities) { SelectEntities(entities); };

		// The editor owns exactly one concrete renderer. Scope prevents inactive
		// renderers from remaining alive through shared references.
		Scope<SceneRenderer> m_SceneRenderer;
		SceneRendererType m_ActiveRendererType = SceneRendererType::PathTracer;
		SceneRendererType m_PendingRendererType = SceneRendererType::PathTracer;
		bool m_RendererRecreationRequested = false;
		glm::uvec2 m_RendererViewportSize = glm::uvec2(1u);
		Ref<EditorPanelManager> m_EditorPanelManager;
		FlyingCamera m_Camera;
		std::vector<Entity> m_Entities;
		std::unordered_map<AssetHandle, MeshBounds> m_MeshBoundsCache;
		std::unordered_map<AssetHandle, std::vector<MeshBounds>> m_SubmeshBoundsCache;
		// Same per-submesh bounds as m_SubmeshBoundsCache, but without each
		// submesh's own node Transform applied -- i.e. in the space a split
		// entity actually renders in, where that Transform is already baked
		// into the entity's own TransformComponent instead of being reapplied
		// (RenderScene::RegisterMesh skips it for a submesh-filtered instance).
		std::unordered_map<AssetHandle, std::vector<MeshBounds>> m_RawSubmeshBoundsCache;

		// Stashed between opening "Split Into Submeshes?" and the user's choice
		// on a later frame.
		AssetHandle m_PendingSplitMeshHandle = 0;
		glm::vec3 m_PendingSplitPlacement = glm::vec3(0.0f);
		// Valid only when the split was triggered by dropping onto a Hierarchy
		// node rather than the viewport.
		Entity m_PendingSplitParent;
	};
}
