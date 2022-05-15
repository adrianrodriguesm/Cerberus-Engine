#pragma once
#include "EditorPanel.h"

namespace cerberus
{
	class SceneHierarchyPanel : public EditorPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& scene);
		virtual void Shutdown() override {};
		virtual void SetSceneContext(const Ref<Scene>& scene) override;
		virtual void SetSelectionContext(const Entity& entity) override;
		void SetSelectionChangedCallback(const std::function<void(Entity)>& func) { m_SelectionChangedCallback = func; }
		void SetEntityDeletedCallback(const std::function<void(Entity)>& func) { m_EntityDeletedCallback = func; }
		virtual void OnImGuiRender(bool& isOpen) override;
	private:
		void DrawEntityNode(Entity& entity);
		void SetSelected(const Entity& entity);
	private:
		Ref<Scene> m_SceneContext;
		Entity m_SelectionContext;

		bool m_IsWindow;

		std::function<void(Entity)> m_SelectionChangedCallback, m_EntityDeletedCallback;
	};
}