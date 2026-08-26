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
		virtual void SetSelectionContext(const std::vector<Entity>& selection) override;
		void SetSelectionChangedCallback(const std::function<void(const std::vector<Entity>&)>& func) { m_SelectionChangedCallback = func; }
		void SetEntityActivatedCallback(const std::function<void(Entity)>& func) { m_EntityActivatedCallback = func; }
		void SetEntityDeletedCallback(const std::function<void(Entity)>& func) { m_EntityDeletedCallback = func; }
		void SetAssetDroppedOnEntityCallback(const std::function<void(AssetHandle, Entity)>& func) { m_AssetDroppedOnEntityCallback = func; }
		virtual void OnImGuiRender(bool& isOpen) override;
	private:
		void DrawEntityNode(Entity& entity);
		// Replaces the whole selection with a single entity (menu-created /
		// duplicated entities) and notifies m_SelectionChangedCallback.
		void SetSelected(const Entity& entity);
		// Applies a tree-node click's selection-set change (plain/Ctrl/Shift)
		// and notifies m_SelectionChangedCallback.
		void HandleEntityClicked(const Entity& entity);
		bool IsSelected(const Entity& entity) const;
	private:
		Ref<Scene> m_SceneContext;
		std::vector<Entity> m_SelectionContext;
		// This frame's flat, visible (not-collapsed) draw order, used to resolve
		// Shift+click range-selection the same way a file explorer would.
		std::vector<Entity> m_VisitedThisFrame;
		Entity m_ShiftAnchorEntity;
		std::vector<Entity> m_EntitiesToDuplicate;
		std::vector<Entity> m_EntitiesToDelete;
		// Reparent requests are deferred like duplicate/delete above -- applying
		// one mid-walk could erase from a RelationshipComponent::Children vector
		// the enclosing DrawEntityNode call is still range-for iterating over.
		Entity m_EntityToReparent;
		Entity m_ReparentTarget;
		Entity m_EntityToUnparent;
		// Deferred like the entity-reparent fields above, for the same reason.
		AssetHandle m_AssetDropHandle = 0;
		Entity m_AssetDropTarget;

		bool m_IsWindow;

		std::function<void(const std::vector<Entity>&)> m_SelectionChangedCallback;
		std::function<void(Entity)> m_EntityActivatedCallback, m_EntityDeletedCallback;
		std::function<void(AssetHandle, Entity)> m_AssetDroppedOnEntityCallback;
	};
}
