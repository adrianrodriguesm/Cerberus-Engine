#pragma once
#include "Cerberus.h"

namespace cerberus
{
	class RuntimeLayer : public Layer
	{
	public:
		RuntimeLayer();
		~RuntimeLayer() = default;
		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Event& event) override;
		virtual void OnUpdate(Timestep ts) override;

		void OpenScene();
		// Events
		bool OnWindowResize(WindowResizeEvent& event);
		bool OnKeyPress(KeyPressedEvent& event);

		void UI_Viewport();
		void UI_DockSpace();

	private:
		Ref<Scene> m_Scene;
		Entity m_SelectedEntity;

		int m_GizmoType = -1;
		bool m_ViewportPanelMouseOver = false, m_ViewportPanelFocused = false;

		Ref<SceneRenderer> m_SceneRenderer;
		FlyingCamera m_Camera;
		std::vector<Entity> m_Entities;
	};
}