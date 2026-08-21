#include "pch.h"
#include "SceneHierarchyPanel.h"
#include "ImGui/ImGuiUI.h"

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
		m_SelectionContext = {};
		m_EntityToDuplicate = {};
		m_EntityToDelete = {};
	}
	void SceneHierarchyPanel::SetSelectionContext(const Entity& entity)
	{
		m_SelectionContext = entity;
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
				m_SceneContext->m_Registry.each([&](const auto& entityID)
				{
					Entity entity = { entityID , m_SceneContext.get() };
					DrawEntityNode(entity);
				});

				// Registry mutations are deferred until iteration is complete so EnTT
				// storage cannot be invalidated while drawing the hierarchy.
				if (m_EntityToDuplicate)
				{
					Entity sourceEntity = m_EntityToDuplicate;
					m_EntityToDuplicate = {};
					Entity duplicateEntity = m_SceneContext->DuplicateEntity(sourceEntity);
					if (duplicateEntity)
						SetSelected(duplicateEntity);
				}

				if (m_EntityToDelete)
				{
					Entity entityToDelete = m_EntityToDelete;
					m_EntityToDelete = {};
					m_SceneContext->DestroyEntity(entityToDelete);
					if (entityToDelete == m_SelectionContext)
						SetSelected({});
				}
			}
		}
		ImGui::End();
	}

	void SceneHierarchyPanel::SetSelected(const Entity& entity)
	{
		m_SelectionContext = entity;
		if (m_SelectionChangedCallback)
			m_SelectionChangedCallback(entity);
	}

	void SceneHierarchyPanel::DrawEntityNode(Entity& entity)
	{
		auto& tag = entity.GetComponent<TagComponent>().Tag;
		ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		bool opened = ImGui::TreeNodeEx((void*)((uint64_t)(uint32_t)entity), flags, tag.c_str());
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			SetSelected(entity);
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_EntityActivatedCallback)
				m_EntityActivatedCallback(entity);
		}

		bool entityDeleted = false;
		bool entityDuplicated = false;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Duplicate Entity", "Ctrl+D"))
				entityDuplicated = true;

			ImGui::Separator();
			if (ImGui::MenuItem("Delete Entity"))
				entityDeleted = true;

			ImGui::EndPopup();
		}

		if (opened)
			ImGui::TreePop();

		if (entityDuplicated)
			m_EntityToDuplicate = entity;
		

		if (entityDeleted)
			m_EntityToDelete = entity;
	}
}
