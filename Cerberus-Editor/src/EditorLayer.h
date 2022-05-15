#pragma once
#include "Cerberus.h"

#include "Panels/EditorPanel.h"
#include "ImGui/ImGuizmo.h"
namespace cerberus
{
	class EditorLayer : public Layer
	{
	public:
		EditorLayer();
		~EditorLayer() = default;
		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Event& event) override;
		virtual void OnUpdate(Timestep ts) override;

		void SelectEntity(const Entity& entity);

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
		Ref<Scene> m_Scene;
		Entity m_SelectedEntity;

		int m_GizmoType = -1;
		bool m_ViewportPanelMouseOver = false, m_ViewportPanelFocused = false;

		std::function<void(Entity)> m_SelectionChangedCallback = [&](const Entity& entity) { SelectEntity(entity); };

		Ref<SceneRenderer> m_SceneRenderer;
		Ref<EditorPanelManager> m_EditorPanelManager;
		FlyingCamera m_Camera;
		std::vector<Entity> m_Entities;
	};
}