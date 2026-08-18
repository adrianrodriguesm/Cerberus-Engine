#pragma once
#include "EditorPanel.h"

namespace cerberus
{
	class ScenePropertiesPanel : public EditorPanel
	{
	public:
		using MaterialTableChangedCallback = std::function<void(const Ref<Mesh>&, const Ref<MaterialTable>&)>;

		ScenePropertiesPanel() = default;
		ScenePropertiesPanel(const Ref<Scene>& sceneContext);

		virtual void Shutdown() override {};
		virtual void OnImGuiRender(bool& isOpen) override;
		virtual void SetSceneContext(const Ref<Scene>& scene) override;

		virtual void SetSelectionContext(const Entity& selectionContext) override;
		void SetMaterialTableChangedCallback(MaterialTableChangedCallback callback) { m_MaterialTableChangedCallback = std::move(callback); }

	private:
		void DrawEntityComponents(Entity entity);
	private:
		Ref<Scene> m_SceneContext;
		Entity m_SelectionContext;
		MaterialTableChangedCallback m_MaterialTableChangedCallback;
	};
}
