#include "pch.h"
#include "SceneHierarchyPanel.h"
#include "ImGui/ImGuiUI.h"
#include <algorithm>

namespace cerberus
{
	static std::string s_AssetPathString;
	SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& scene)
		: m_SceneContext(scene)
	{
		s_AssetPathString = AssetManager::GetAssetPath().string();
	}
	void SceneHierarchyPanel::SetSceneContext(const Ref<Scene>& sceneContext)
	{
		m_SceneContext = sceneContext;
		m_SelectionContext.clear();
		m_ShiftAnchorEntity = {};
		m_EntitiesToDuplicate.clear();
		m_EntitiesToDelete.clear();
		m_EntityToReparent = {};
		m_ReparentTarget = {};
		m_EntityToUnparent = {};
		m_AssetDropHandle = 0;
		m_AssetDropTarget = {};
	}
	void SceneHierarchyPanel::SetSelectionContext(const std::vector<Entity>& selection)
	{
		m_SelectionContext = selection;
	}
	bool SceneHierarchyPanel::IsSelected(const Entity& entity) const
	{
		return std::find(m_SelectionContext.begin(), m_SelectionContext.end(), entity) != m_SelectionContext.end();
	}
	void SceneHierarchyPanel::OnImGuiRender(bool& isOpen)
	{
		ImGui::Begin("Scene Hierarchy", &isOpen);
		{
			if (m_SceneContext)
			{
				if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
				{
					if (ImGui::BeginMenu("Create"))
					{
						if (ImGui::MenuItem("Empty Entity"))
						{
							auto newEntity = m_SceneContext->CreateEntity("Empty Entity");
							SetSelected(newEntity);
						}
						if (ImGui::BeginMenu("3D"))
						{
							if (ImGui::MenuItem("Cube"))
							{
								auto newEntity = m_SceneContext->CreateEntity("Cube");
								auto mesh = AssetManager::GetAssetHandleFromFilePath(s_AssetPathString + "/Meshes/Default/Cube.cmesh");
								newEntity.AddComponent<MeshComponent>(mesh);
								SetSelected(newEntity);
							}
							if (ImGui::MenuItem("Sphere"))
							{
								auto newEntity = m_SceneContext->CreateEntity("Sphere");
								auto mesh = AssetManager::GetAssetHandleFromFilePath(s_AssetPathString + "/Meshes/Default/Sphere.cmesh");
								newEntity.AddComponent<MeshComponent>(mesh);
								SetSelected(newEntity);
							}
							if (ImGui::MenuItem("Capsule"))
							{
								auto newEntity = m_SceneContext->CreateEntity("Capsule");
								auto mesh = AssetManager::GetAssetHandleFromFilePath(s_AssetPathString + "/Meshes/Default/Capsule.cmesh");
								newEntity.AddComponent<MeshComponent>(mesh);
								SetSelected(newEntity);
							}
							if (ImGui::MenuItem("Cylinder"))
							{
								auto newEntity = m_SceneContext->CreateEntity("Cylinder");
								auto mesh = AssetManager::GetAssetHandleFromFilePath(s_AssetPathString + "/Meshes/Default/Cylinder.cmesh");
								newEntity.AddComponent<MeshComponent>(mesh);
								SetSelected(newEntity);
							}
							if (ImGui::MenuItem("Torus"))
							{
								auto newEntity = m_SceneContext->CreateEntity("Torus");
								auto mesh = AssetManager::GetAssetHandleFromFilePath(s_AssetPathString + "/Meshes/Default/Torus.cmesh");
								newEntity.AddComponent<MeshComponent>(mesh);
								SetSelected(newEntity);
							}
							if (ImGui::MenuItem("Plane"))
							{
								auto newEntity = m_SceneContext->CreateEntity("Plane");
								auto mesh = AssetManager::GetAssetHandleFromFilePath(s_AssetPathString + "/Meshes/Default/Plane.cmesh");
								newEntity.AddComponent<MeshComponent>(mesh);
								SetSelected(newEntity);
							}
							if (ImGui::MenuItem("Cone"))
							{
								auto newEntity = m_SceneContext->CreateEntity("Cone");
								auto mesh = AssetManager::GetAssetHandleFromFilePath(s_AssetPathString + "/Meshes/Default/Cone.cmesh");
								newEntity.AddComponent<MeshComponent>(mesh);
								SetSelected(newEntity);
							}
							ImGui::EndMenu();
						}
						/**/
						ImGui::Separator();
						if (ImGui::MenuItem("Directional Light"))
						{
							auto newEntity = m_SceneContext->CreateEntity("Directional Light");
							newEntity.AddComponent<DirectionalLightComponent>();
							newEntity.GetComponent<TransformComponent>().SetRotation(glm::radians(glm::vec3{ 80.0f, 10.0f, 0.0f }));
							SetSelected(newEntity);
						}
						if (ImGui::MenuItem("Sky Light"))
						{
							auto newEntity = m_SceneContext->CreateEntity("Sky Light");
							newEntity.AddComponent<SkyLightComponent>();
							SetSelected(newEntity);
						}
						ImGui::EndMenu();
					}
					ImGui::EndPopup();
				}
				// Only draw root entities here; DrawEntityNode recurses into
				// RelationshipComponent::Children for the rest of the tree.
				// m_VisitedThisFrame is rebuilt alongside the draw so Shift+click
				// range-selection matches this frame's actual visible order.
				m_VisitedThisFrame.clear();
				m_SceneContext->m_Registry.each([&](const auto& entityID)
				{
					Entity entity = { entityID , m_SceneContext.get() };
					if (!entity.GetComponent<RelationshipComponent>().Parent)
						DrawEntityNode(entity);
				});

				// Registry mutations are deferred until iteration is complete so EnTT
				// storage cannot be invalidated while drawing the hierarchy.
				if (m_EntityToReparent)
				{
					Entity child = m_EntityToReparent;
					Entity target = m_ReparentTarget;
					m_EntityToReparent = {};
					m_ReparentTarget = {};
					m_SceneContext->ReparentEntityPreservingWorldTransform(child, target);
				}

				if (m_EntityToUnparent)
				{
					Entity child = m_EntityToUnparent;
					m_EntityToUnparent = {};
					m_SceneContext->UnparentEntityPreservingWorldTransform(child);
				}

				if (m_AssetDropHandle)
				{
					AssetHandle handle = m_AssetDropHandle;
					Entity target = m_AssetDropTarget;
					m_AssetDropHandle = 0;
					m_AssetDropTarget = {};
					if (m_AssetDroppedOnEntityCallback)
						m_AssetDroppedOnEntityCallback(handle, target);
				}

				if (!m_EntitiesToDuplicate.empty())
				{
					std::vector<Entity> sourceEntities = m_EntitiesToDuplicate;
					m_EntitiesToDuplicate.clear();
					std::vector<Entity> duplicates;
					for (Entity sourceEntity : sourceEntities)
					{
						Entity duplicateEntity = m_SceneContext->DuplicateEntity(sourceEntity);
						if (duplicateEntity)
							duplicates.push_back(duplicateEntity);
					}
					if (!duplicates.empty())
					{
						m_SelectionContext = duplicates;
						m_ShiftAnchorEntity = duplicates.back();
						if (m_SelectionChangedCallback)
							m_SelectionChangedCallback(m_SelectionContext);
					}
				}

				if (!m_EntitiesToDelete.empty())
				{
					std::vector<Entity> entitiesToDelete = m_EntitiesToDelete;
					m_EntitiesToDelete.clear();
					for (Entity entityToDelete : entitiesToDelete)
						m_SceneContext->DestroyEntity(entityToDelete);
					// A batch delete invalidates any leftover selection; clear it
					// outright rather than reasoning about a partial remainder.
					m_SelectionContext.clear();
					m_ShiftAnchorEntity = {};
					if (m_SelectionChangedCallback)
						m_SelectionChangedCallback(m_SelectionContext);
				}
			}
		}
		ImGui::End();
	}

	void SceneHierarchyPanel::SetSelected(const Entity& entity)
	{
		m_SelectionContext = { entity };
		m_ShiftAnchorEntity = entity;
		if (m_SelectionChangedCallback)
			m_SelectionChangedCallback(m_SelectionContext);
	}

	void SceneHierarchyPanel::HandleEntityClicked(const Entity& entity)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (io.KeyShift && m_ShiftAnchorEntity)
		{
			auto anchorIt = std::find(m_VisitedThisFrame.begin(), m_VisitedThisFrame.end(), m_ShiftAnchorEntity);
			auto clickedIt = std::find(m_VisitedThisFrame.begin(), m_VisitedThisFrame.end(), entity);
			if (anchorIt != m_VisitedThisFrame.end() && clickedIt != m_VisitedThisFrame.end())
			{
				const size_t startIndex = std::min(anchorIt - m_VisitedThisFrame.begin(), clickedIt - m_VisitedThisFrame.begin());
				const size_t endIndex = std::max(anchorIt - m_VisitedThisFrame.begin(), clickedIt - m_VisitedThisFrame.begin());
				m_SelectionContext.assign(m_VisitedThisFrame.begin() + startIndex, m_VisitedThisFrame.begin() + endIndex + 1);
			}
			else
			{
				// Anchor no longer visible this frame (e.g. its parent got
				// collapsed) -- fall back to a plain select on the clicked entity.
				m_SelectionContext = { entity };
				m_ShiftAnchorEntity = entity;
			}
		}
		else if (io.KeyCtrl)
		{
			auto it = std::find(m_SelectionContext.begin(), m_SelectionContext.end(), entity);
			if (it != m_SelectionContext.end())
				m_SelectionContext.erase(it);
			else
				m_SelectionContext.push_back(entity);
			m_ShiftAnchorEntity = entity;
		}
		else
		{
			m_SelectionContext = { entity };
			m_ShiftAnchorEntity = entity;
		}

		if (m_SelectionChangedCallback)
			m_SelectionChangedCallback(m_SelectionContext);
	}

	void SceneHierarchyPanel::DrawEntityNode(Entity& entity)
	{
		m_VisitedThisFrame.push_back(entity);

		auto& tag = entity.GetComponent<TagComponent>().Tag;
		auto& relationship = entity.GetComponent<RelationshipComponent>();
		const bool hasChildren = !relationship.Children.empty();

		ImGuiTreeNodeFlags flags = (IsSelected(entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!hasChildren)
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

		bool opened = ImGui::TreeNodeEx((void*)((uint64_t)(uint32_t)entity), flags, tag.c_str());
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			HandleEntityClicked(entity);
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_EntityActivatedCallback)
				m_EntityActivatedCallback(entity);
		}
		// Right-click on an entity outside the current selection replaces the
		// selection with just it first, so the context menu below always acts
		// on a selection that includes the entity you right-clicked -- matching
		// the left-click-to-select-then-act convention used elsewhere.
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsSelected(entity))
			SetSelected(entity);

		if (ImGui::BeginDragDropSource())
		{
			UUID entityID = entity.GetComponent<IDComponent>().ID;
			ImGui::SetDragDropPayload("entity_payload", &entityID, sizeof(UUID));
			ImGui::TextUnformatted(tag.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("entity_payload"))
			{
				UUID droppedID = *static_cast<const UUID*>(payload->Data);
				Entity dropped = m_SceneContext->GetEntityByUUID(droppedID);
				if (dropped)
				{
					m_EntityToReparent = dropped;
					m_ReparentTarget = entity;
				}
			}
			// Same payload the Content Browser already produces for the
			// viewport drop path -- this panel doesn't need to know what kind
			// of asset it is, EditorLayer::OnAssetDroppedOnEntity resolves that.
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("asset_payload"))
			{
				m_AssetDropHandle = *static_cast<const AssetHandle*>(payload->Data);
				m_AssetDropTarget = entity;
			}
			ImGui::EndDragDropTarget();
		}

		bool entityDeleted = false;
		bool entityDuplicated = false;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Duplicate Entity", "Ctrl+D"))
				entityDuplicated = true;

			ImGui::Separator();
			if (ImGui::MenuItem("Unparent", nullptr, false, relationship.Parent != 0))
				m_EntityToUnparent = entity;

			ImGui::Separator();
			if (ImGui::MenuItem("Delete Entity"))
				entityDeleted = true;

			ImGui::EndPopup();
		}

		if (hasChildren && opened)
		{
			for (UUID childID : relationship.Children)
			{
				Entity child = m_SceneContext->GetEntityByUUID(childID);
				if (child)
					DrawEntityNode(child);
			}
			ImGui::TreePop();
		}

		// The right-click handling above guarantees entity is part of
		// m_SelectionContext by this point, so these bulk-apply to the whole
		// current selection rather than just entity.
		if (entityDuplicated)
			m_EntitiesToDuplicate = m_SelectionContext;

		if (entityDeleted)
			m_EntitiesToDelete = m_SelectionContext;
	}
}
