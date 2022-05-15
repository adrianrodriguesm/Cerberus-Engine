#pragma once
#include "EditorPanel.h"

namespace cerberus
{
	class ScenePropertiesPanel : public EditorPanel
	{
	public:
		ScenePropertiesPanel() = default;
		ScenePropertiesPanel(const Ref<Scene>& sceneContext);

		virtual void Shutdown() override {};
		virtual void OnImGuiRender(bool& isOpen) override;
		virtual void SetSceneContext(const Ref<Scene>& scene) override;

		virtual void SetSelectionContext(const Entity& selectionContext) override;

	private:
		void DrawEntityComponents(Entity entity);
	private:
		Ref<Scene> m_SceneContext;
		Entity m_SelectionContext;
	};
}