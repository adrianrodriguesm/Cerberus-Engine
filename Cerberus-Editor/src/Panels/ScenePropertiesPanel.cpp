#include "pch.h"
#include "ScenePropertiesPanel.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "ImGui/ImGuiUI.h"
#include "Renderer/Core/Renderer.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Utils/FileDialogs.h"
#include "Utils/FileSystem.h"
namespace cerberus
{
	static std::filesystem::path s_AssetPath;
	ScenePropertiesPanel::ScenePropertiesPanel(const Ref<Scene>& sceneContext)
		: m_SceneContext(sceneContext)
	{
		s_AssetPath = AssetManager::GetAssetPath();
	}
	void ScenePropertiesPanel::OnImGuiRender(bool& isOpen)
	{
		if (!ImGui::Begin("Properties Panel", &isOpen))
		{
			ImGui::End();
			return;
		}

		m_SceneContext->m_Registry.each([&](const auto& entityID)
		{
			Entity entity = { entityID , m_SceneContext.get() };
			if (m_SelectionContext == entity)
				DrawEntityComponents(entity);
		});
		ImGui::End();
	}
	void ScenePropertiesPanel::SetSceneContext(const Ref<Scene>& scene)
	{
		m_SceneContext = scene;
		m_SelectionContext = {};
	}
	void ScenePropertiesPanel::SetSelectionContext(const Entity& selectionContext)
	{
		m_SelectionContext = selectionContext;
	}

	static void DrawVec3Control(const std::string& label, glm::vec3& values, float speed = 0.1f, float resetValue = 0.f, float columnWidth = 100.0f)
	{

		auto boldFont = ImGui::GetIO().Fonts->Fonts[0];
		// Unique label for each Vec3
		ImGui::PushID(label.c_str());
		{
			// two columns: Label(Translation) | Value(Vec3)
			ImGui::Columns(2);
			ImGui::SetColumnWidth(0, columnWidth);

			ImGui::Text(label.c_str());
			ImGui::NextColumn();

			ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
			{
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
				{
					float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
					ImVec2 buttonSize = { lineHeight * 1.5f, lineHeight };

					// X
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.5f, 0.1f, 0.15f, 1.0f });
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.8f, 0.2f, 0.2f, 1.0f });
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.5f, 0.1f, 0.15f, 1.0f });
					{
						ImGui::PushFont(boldFont);
						if (ImGui::Button("X", buttonSize))
							values.x = resetValue;
						ImGui::PopFont();
						ImGui::SameLine();
						ImGui::DragFloat("##X", &values.x, speed, 0.f, 0.f, "%.2f");
						ImGui::PopItemWidth();
						ImGui::SameLine();
					}
					ImGui::PopStyleColor(3);
					// Y
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.5f, 0.2f, 1.0f });
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.7f, 0.3f, 1.0f });
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.5f, 0.2f, 1.0f });
					{
						ImGui::PushFont(boldFont);
						if (ImGui::Button("Y", buttonSize))
							values.y = resetValue;
						ImGui::PopFont();
						ImGui::SameLine();
						ImGui::DragFloat("##Y", &values.y, speed, 0.f, 0.f, "%.2f");
						ImGui::PopItemWidth();
						ImGui::SameLine();
					}
					ImGui::PopStyleColor(3);
					// Z
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.5f, 1.0f });
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.8f, 1.0f });
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.5f, 1.0f });
					{
						ImGui::PushFont(boldFont);
						if (ImGui::Button("Z", buttonSize))
							values.z = resetValue;
						ImGui::PopFont();
						ImGui::SameLine();
						ImGui::DragFloat("##Z", &values.z, speed, 0.f, 0.f, "%.2f");
						ImGui::PopItemWidth();
					}
					ImGui::PopStyleColor(3);
				}
				ImGui::PopStyleVar();
			}

			ImGui::Columns(1);
		}
		ImGui::PopID();

	}

	template<typename T, typename UFunction>
	static void DrawComponent(const std::string& name, Entity entity, UFunction uiFunction)
	{
		constexpr ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
			ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap |
			ImGuiTreeNodeFlags_FramePadding;
		/**/
		if (entity.HasComponent<T>())
		{
			ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
			float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 1.0f; // Needs to be calculated after setting the frame padding
			ImGui::Separator();

			bool isComponentOpen = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, name.c_str());

			ImGui::PopStyleVar();
			/**/
			ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);

		
			if (ImGui::Button("+", ImVec2{ lineHeight, lineHeight }))
				ImGui::OpenPopup("ComponentSettings");
			

			bool removedComponent = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				if (ImGui::MenuItem("Remove Component"))
					removedComponent = true;

				ImGui::EndPopup();
			}
			/**/
			if (isComponentOpen)
			{
				auto& component = entity.GetComponent<T>();
				uiFunction(component);
				ImGui::TreePop();
			}
			/**/
			if (removedComponent)
				entity.RemovedComponent<T>();
			/**/
		}
		/**/
	}

	void ScenePropertiesPanel::DrawEntityComponents(Entity entity)
	{

		if (entity.HasComponent<TagComponent>())
		{
			auto& tag = entity.GetComponent<TagComponent>().Tag;
			// Maximum of 256 characters
			char buffer[256];
			memset(buffer, 0, sizeof(buffer));
			strcpy_s(buffer, sizeof(buffer), tag.c_str());
			if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
			{
				tag = std::string(buffer);
			}
		}
		ImGui::SameLine();
		ImGui::PushItemWidth(-1);
		{
			{
				const ImU32 buttonCol = IM_COL32(0, 230, 255, 90);
				const ImU32 buttonHov = IM_COL32(153, 255, 255, 100);
				const ImU32 buttonAct = IM_COL32(0, 230, 255, 100);

				ui::ScopedColourStack buttonColours(ImGuiCol_Button, buttonCol,
					ImGuiCol_ButtonHovered, buttonHov,
					ImGuiCol_ButtonActive, buttonAct);
				// Add components
				if (ImGui::Button("Add Component"))
					ImGui::OpenPopup("AddComponent");
			}


			if (ImGui::BeginPopup("AddComponent"))
			{
				if (ImGui::MenuItem("Mesh"))
				{
					if (!entity.HasComponent<MeshComponent>())
					{
						m_SelectionContext.AddComponent<MeshComponent>();
					}
					else
						NV_CORE_WARN("Entity already have a Mesh Component!");
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Sky Light"))
				{
					m_SelectionContext.AddComponent<SkyLightComponent>();
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Directional Light"))
				{
					m_SelectionContext.AddComponent<DirectionalLightComponent>();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		ImGui::PopItemWidth();

		DrawComponent<TransformComponent>("TRANSFORM", entity, [](auto& component)
		{
			// Translation
			glm::vec3 translation = component.GetTranslation();
			DrawVec3Control("Translation", translation);
			component.SetTranslation(translation);

			// Rotation
			glm::vec3 rotation = component.GetRotation();
			DrawVec3Control("Rotation", rotation, 1.f);
			component.SetRotation(rotation);

			// Scale
			glm::vec3 scale = component.GetScale();
			DrawVec3Control("Scale", scale, 0.1f, 1.f);
			component.SetScale(scale);
		});

		DrawComponent<DirectionalLightComponent>("DIRECTIONAL LIGHT", entity, [](auto& component)
		{
			ui::BeginPropertyGrid();
			{
				auto& radiance = component.Radiance;
				ui::Property("Radiance", radiance, 0.05f);
				ui::PropertySlider("Intensity", component.Intensity, 0.f, 100.f);
			}
			ui::EndPropertyGrid();
		});

		const RenderObjectID renderObjectID{ entity.GetComponent<IDComponent>().ID };
		DrawComponent<MeshComponent>("MESH", entity, [this, renderObjectID](auto& component)
		{
			ui::BeginPropertyGrid();
			auto& meshHandle = component.MeshHandle;
			bool succeeded = ui::PropertyAssetReferenceTarget<Mesh>("Mesh", meshHandle);
			Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(meshHandle);
			ui::EndPropertyGrid();

			if (mesh && ui::BeginTreeNode("Material Table", true))
			{
				ui::BeginPropertyGrid();
				const auto& meshMaterialTable = mesh->GetMaterialTable();
				uint32_t materialCount = meshMaterialTable->GetMaterialCount();
				// New mesh update the material table
				if (succeeded || !component.MaterialTable)
					component.MaterialTable = CreateRef<MaterialTable>(meshMaterialTable);

				ui::PropertyAssetReferenceSettings settings;
				settings.AdvanceToNextColumn = false;
				settings.WidthOffset = ImGui::GetStyle().ItemSpacing.x + 28.0f;
				settings.OnClick = [this](AssetHandle materialHandle)
				{
					if (m_AssetFocusedCallback)
						m_AssetFocusedCallback(materialHandle);
				};
				for (size_t i = 0; i < materialCount; i++)
				{
					std::string label = fmt::format("[Material {0}]", i);
					AssetHandle materialAssetHandle = component.MaterialTable->HasMaterial(static_cast<uint32_t>(i))
						? component.MaterialTable->GetMaterial(static_cast<uint32_t>(i))
						: AssetHandle(0);

					const bool materialChanged = ui::PropertyAssetReferenceTarget<MaterialAsset>(label.c_str(), materialAssetHandle, [i, materialTable = component.MaterialTable](Ref<MaterialAsset> materialAsset) mutable
					{
						materialTable->SetMaterial(static_cast<uint32_t>(i), materialAsset->Handle);
					}, settings);


					ImGui::SameLine();
					float prevItemHeight = ImGui::GetItemRectSize().y;
					bool materialCleared = false;
					if (ImGui::Button(fmt::format("X##Material{}", i).c_str(), { prevItemHeight, prevItemHeight }))
					{
						if (component.MaterialTable->HasMaterial(static_cast<uint32_t>(i)))
							component.MaterialTable->ClearMaterial(static_cast<uint32_t>(i));
						materialCleared = true;
					}

					if ((materialChanged || materialCleared) && m_MaterialTableChangedCallback)
						m_MaterialTableChangedCallback(renderObjectID, component.MaterialTable);

					ImGui::NextColumn();
					
				}
				ui::EndPropertyGrid();
				ui::EndTreeNode();
			}
		
			
		});

		DrawComponent<SkyLightComponent>("SKY LIGHT", entity, [this](auto& component) mutable
		{
			ui::BeginPropertyGrid();
			ui::PropertyAssetReferenceTarget<EnvironmentMap>("Environment", component.EnvironmentHandle);
			ui::EndPropertyGrid();
			Ref<EnvironmentMap> envMap = AssetManager::GetAsset<EnvironmentMap>(component.EnvironmentHandle);

			const uint32_t environmentResolution = Renderer::GetRendererConfig().EnvironmentMapResolution;
			uint32_t maxSampleLOD = Image::CalculateMipCount(environmentResolution, environmentResolution) - 1;
			if (envMap && envMap->RadianceMap.IsValid())
			{
				const TextureCube* radianceMap = ResourceManager::GetTextureCube(envMap->RadianceMap);
				if (radianceMap && radianceMap->GetMipLevelCount() > 0)
					maxSampleLOD = radianceMap->GetMipLevelCount() - 1;
			}

			component.SampleLOD = glm::min(component.SampleLOD, maxSampleLOD);
			int sampleLOD = static_cast<int>(component.SampleLOD);
			if (envMap)
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
				ui::Image(envMap->EquirectangularTex, { 256, 128 });
				ImGui::PopStyleVar();
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::PopTextWrapPos();
					ui::Image(envMap->EquirectangularTex, { 384, 192 });
					ImGui::EndTooltip();
				}
			}
			ui::BeginPropertyGrid();
			ui::Property("Intensity", component.Intensity, 0.05f, 0.f, 100.f);
			if (ui::PropertySlider("Sample LOD", sampleLOD, 0, static_cast<int>(maxSampleLOD)))
				component.SampleLOD = static_cast<uint32_t>(sampleLOD);
			ui::EndPropertyGrid();
		});
		
		
	}
}
