#include "pch.h"
#include "ScenePropertiesPanel.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "ImGui/ImGuiUI.h"
#include "Renderer/Core/Renderer.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Utils/FileDialogs.h"
#include "Utils/FileSystem.h"
#include <algorithm>
namespace cerberus
{
	static std::filesystem::path s_AssetPath;

	// Copied Transform values, not a live reference -- "copiar y pegar
	// valores," shared across single- and multi-entity editing below.
	struct TransformClipboard
	{
		bool Filled = false;
		glm::vec3 Translation{ 0.0f }, Rotation{ 0.0f }, Scale{ 1.0f };
	};
	static TransformClipboard s_TransformClipboard;
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

		// Cross-reference against the live registry rather than trusting
		// m_SelectionContext directly -- a selected entity destroyed elsewhere
		// this frame would otherwise leave a dangling handle here.
		std::vector<Entity> liveSelection;
		m_SceneContext->m_Registry.each([&](const auto& entityID)
		{
			Entity entity = { entityID , m_SceneContext.get() };
			if (std::find(m_SelectionContext.begin(), m_SelectionContext.end(), entity) != m_SelectionContext.end())
				liveSelection.push_back(entity);
		});

		if (liveSelection.size() == 1)
			DrawEntityComponents(liveSelection[0]);
		else if (liveSelection.size() > 1)
			DrawMultiEntityTransform(liveSelection);

		ImGui::End();
	}
	void ScenePropertiesPanel::SetSceneContext(const Ref<Scene>& scene)
	{
		m_SceneContext = scene;
		m_SelectionContext.clear();
	}
	void ScenePropertiesPanel::SetSelectionContext(const std::vector<Entity>& selectionContext)
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
						entity.AddComponent<MeshComponent>();
					}
					else
						NV_CORE_WARN("Entity already have a Mesh Component!");
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Sky Light"))
				{
					entity.AddComponent<SkyLightComponent>();
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Directional Light"))
				{
					entity.AddComponent<DirectionalLightComponent>();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		ImGui::PopItemWidth();

		DrawComponent<TransformComponent>("TRANSFORM", entity, [](auto& component)
		{
			if (ImGui::Button("Copy"))
			{
				s_TransformClipboard.Translation = component.GetTranslation();
				s_TransformClipboard.Rotation = component.GetRotation();
				s_TransformClipboard.Scale = component.GetScale();
				s_TransformClipboard.Filled = true;
			}
			ImGui::SameLine();
			if (s_TransformClipboard.Filled)
			{
				if (ImGui::Button("Paste"))
				{
					component.SetTranslation(s_TransformClipboard.Translation);
					component.SetRotation(s_TransformClipboard.Rotation);
					component.SetScale(s_TransformClipboard.Scale);
				}
			}
			else
			{
				ImGui::TextDisabled("Paste");
			}

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

	void ScenePropertiesPanel::DrawMultiEntityTransform(const std::vector<Entity>& entities)
	{
		ImGui::Text("%zu entities selected", entities.size());

		if (entities.empty())
			return;

		// "Active object" convention -- same entity the group gizmo in
		// EditorLayer::UI_Viewport() uses as its pivot, so this panel and the
		// gizmo agree on which entity's values are being shown.
		Entity representative = entities.back();
		auto& representativeTransform = representative.GetComponent<TransformComponent>();

		if (ImGui::Button("Copy"))
		{
			s_TransformClipboard.Translation = representativeTransform.GetTranslation();
			s_TransformClipboard.Rotation = representativeTransform.GetRotation();
			s_TransformClipboard.Scale = representativeTransform.GetScale();
			s_TransformClipboard.Filled = true;
		}
		ImGui::SameLine();
		if (s_TransformClipboard.Filled)
		{
			// Paste is absolute -- every selected entity gets the exact
			// copied transform, unlike dragging a field below.
			if (ImGui::Button("Paste"))
			{
				for (Entity entity : entities)
				{
					auto& tc = entity.GetComponent<TransformComponent>();
					tc.SetTranslation(s_TransformClipboard.Translation);
					tc.SetRotation(s_TransformClipboard.Rotation);
					tc.SetScale(s_TransformClipboard.Scale);
				}
			}
		}
		else
		{
			ImGui::TextDisabled("Paste");
		}

		if (!ImGui::CollapsingHeader("TRANSFORM", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		// Dragging/typing here is a relative (delta) edit applied to every
		// selected entity, preserving each one's own current value/offset --
		// the same incremental-transform model the group gizmo already uses,
		// rather than collapsing every selected entity onto one absolute value.
		glm::vec3 translation = representativeTransform.GetTranslation();
		const glm::vec3 oldTranslation = translation;
		DrawVec3Control("Translation", translation);
		if (translation != oldTranslation)
		{
			const glm::vec3 delta = translation - oldTranslation;
			for (Entity entity : entities)
			{
				auto& tc = entity.GetComponent<TransformComponent>();
				tc.SetTranslation(tc.GetTranslation() + delta);
			}
		}

		glm::vec3 rotation = representativeTransform.GetRotation();
		const glm::vec3 oldRotation = rotation;
		DrawVec3Control("Rotation", rotation, 1.f);
		if (rotation != oldRotation)
		{
			const glm::vec3 delta = rotation - oldRotation;
			for (Entity entity : entities)
			{
				auto& tc = entity.GetComponent<TransformComponent>();
				tc.SetRotation(tc.GetRotation() + delta);
			}
		}

		glm::vec3 scale = representativeTransform.GetScale();
		const glm::vec3 oldScale = scale;
		DrawVec3Control("Scale", scale, 0.1f, 1.f);
		if (scale != oldScale)
		{
			const glm::vec3 delta = scale - oldScale;
			for (Entity entity : entities)
			{
				auto& tc = entity.GetComponent<TransformComponent>();
				tc.SetScale(tc.GetScale() + delta);
			}
		}
	}
}
