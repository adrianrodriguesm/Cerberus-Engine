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

		void SelectEntity(const Entity& entity);
		void DuplicateSelectedEntity();
		void FocusEntity(Entity entity);

		void NewScene();
		void OpenScene();
		void OpenScene(const Ref<Scene>& scene);
		void SaveSceneAs();
		void SaveScene();
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
		AssetHandle ResolveDroppedMesh(AssetHandle assetHandle);
		Entity CreateMeshEntityFromDrop(AssetHandle meshHandle, const ViewportRay& ray, const ViewportHit* surfaceHit);
		bool ApplyMaterialFromDrop(AssetHandle materialHandle, const ViewportHit& hit);
		void RequestSceneRenderer(SceneRendererType rendererType);
		void RecreateSceneRenderer();

		Ref<Scene> m_Scene;
		Entity m_SelectedEntity;

		int m_GizmoType = -1;
		bool m_ViewportPanelMouseOver = false, m_ViewportPanelFocused = false;
		bool m_IsViewportMaximized = false;

		std::function<void(Entity)> m_SelectionChangedCallback = [&](const Entity& entity) { SelectEntity(entity); };

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
	};
}
