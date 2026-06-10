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
			}
			m_SceneContext->m_Registry.each([&](const auto& entityID)
			{
				Entity entity = { entityID , m_SceneContext.get() };
				DrawEntityNode(entity);
			});
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
		if (ImGui::IsItemClicked())
			SetSelected(entity);

		bool entityDeleted = false;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Delete Entity"))
				entityDeleted = true;

			ImGui::EndPopup();
		}

		if (opened)
			ImGui::TreePop();
		

		if (entityDeleted)
		{
			m_SceneContext->DestroyEntity(entity);
			if (entity == m_SelectionContext)
				SetSelected({});
		}
	}
}