#pragma once
#include "EditorPanel.h"
#include "Renderer/RenderObjectID.h"

namespace cerberus
{
	class ScenePropertiesPanel : public EditorPanel
	{
	public:
		using MaterialTableChangedCallback = std::function<void(RenderObjectID, const Ref<MaterialTable>&)>;
		using AssetFocusedCallback = std::function<void(AssetHandle)>;

		ScenePropertiesPanel() = default;
		ScenePropertiesPanel(const Ref<Scene>& sceneContext);

		virtual void Shutdown() override {};
		virtual void OnImGuiRender(bool& isOpen) override;
		virtual void SetSceneContext(const Ref<Scene>& scene) override;

		virtual void SetSelectionContext(const Entity& selectionContext) override;
		void SetMaterialTableChangedCallback(MaterialTableChangedCallback callback) { m_MaterialTableChangedCallback = std::move(callback); }
		void SetAssetFocusedCallback(AssetFocusedCallback callback) { m_AssetFocusedCallback = std::move(callback); }

	private:
		void DrawEntityComponents(Entity entity);
	private:
		Ref<Scene> m_SceneContext;
		Entity m_SelectionContext;
		MaterialTableChangedCallback m_MaterialTableChangedCallback;
		AssetFocusedCallback m_AssetFocusedCallback;
	};
}
