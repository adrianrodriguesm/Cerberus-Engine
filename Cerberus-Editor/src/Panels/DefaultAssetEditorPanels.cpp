#include "pch.h"
#include "DefaultAssetEditorPanels.h"
#include "ImGui/ImGuiUI.h"
#include "Utils/FileSystem.h"
#include "Asset/AssetManager.h"
#include "Renderer/RendererResources.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/TextureAsset.h"
namespace cerberus
{
	static std::filesystem::path s_MeshAssetPath = "Resources/Meshes";

	MeshPanel::MeshPanel()
		: AssetEditorPanel("MeshPanel")
	{
	}

	MeshPanel::~MeshPanel()
	{
	}

	void MeshPanel::SetAsset(const Ref<Asset>& asset)
	{
		m_MeshSource = std::dynamic_pointer_cast<MeshSource>(asset);
		m_FilepathBuffer.fill(0);
	}

	void MeshPanel::Render()
	{
		if (!m_MeshSource)
		{
			ImGui::TextDisabled("No mesh source selected.");
			return;
		}

		if (ui::BeginTreeNode("Materials"))
		{
			ui::BeginPropertyGrid();

			auto& materials = m_MeshSource->GetMaterialsAsset();
			bool materialsChanged = false;

			for (size_t i = 0; i < materials.size(); i++)
			{
				std::string label = fmt::format("[Material {0}]", i);
				AssetHandle materialAssetHandle = materials[i] ? materials[i]->Handle : AssetHandle(0);
				materialsChanged |= ui::PropertyAssetReferenceTarget<MaterialAsset>(label.c_str(), materialAssetHandle,
					[&materialSlot = materials[i]](const Ref<MaterialAsset>& materialAsset)
					{
						materialSlot = materialAsset;
					});
			}

			ui::EndPropertyGrid();
			ui::EndTreeNode();

			if (materialsChanged)
			{
				AssetImporter::Serialize(m_MeshSource);
				AssetEditorManager::NotifyAssetModified(m_MeshSource);
			}
		}
		static std::filesystem::path assetPath = FileSystem::GetProjectConfig().AssetDirectory;
		ImGui::Text("Path: %s", assetPath.string().c_str());
		ImGui::SameLine();
		const AssetMetadata& assetData = AssetManager::GetMetadata(m_MeshSource->Handle);

		std::string filepath = fmt::format("Meshes/{0}.{1}", assetData.FilePath.stem().string(), "cmesh");
		if (!m_FilepathBuffer[0])
			strcpy(m_FilepathBuffer.data(), filepath.c_str());

		ImGui::InputText("##meshPath", m_FilepathBuffer.data(), m_FilepathBuffer.size());
		if (ImGui::Button("Create"))
		{
			std::string serializePath = m_FilepathBuffer.data();
			serializePath = assetPath.string() + "/"+ serializePath;
			std::filesystem::path path = serializePath;
			if (!FileSystem::Exists(path.parent_path()))
				FileSystem::CreateDirectory(path.parent_path());

			Ref<Mesh> mesh = AssetManager::CreateNewAsset<Mesh>(path.filename().string(), path.parent_path().string(), m_MeshSource);
		}
		ImGui::SameLine();
	}

	void MeshPanel::OnClose()
	{
		m_MeshSource = nullptr;
	}

	MaterialPanel::MaterialPanel()
		: AssetEditorPanel("MaterialPanel")
	{
		const AssetHandle checkerboardAsset = AssetManager::GetAssetHandleFromFilePath("Resources/Editor/Checkerboard.png");
		m_CheckerboardTex = GetTexturePreview(checkerboardAsset, TextureColorSpace::SRGB);
	}
	MaterialPanel::~MaterialPanel()
	{
	}
	void MaterialPanel::Shutdown()
	{
		CommitChanges();
		m_Material = nullptr;
		DestroyTexturePreviews();
	}
	void MaterialPanel::SetAsset(const Ref<Asset>& asset)
	{
		Ref<MaterialAsset> material = std::dynamic_pointer_cast<MaterialAsset>(asset);
		if (material != m_Material)
			CommitChanges();

		m_Material = material;
	}

	Handle<Texture2D> MaterialPanel::GetTexturePreview(AssetHandle textureAsset, TextureColorSpace colorSpace)
	{
		if (textureAsset == 0)
			return m_CheckerboardTex.IsValid() ? m_CheckerboardTex : RendererResources::GetWhiteTexture();

		auto& previewCache = colorSpace == TextureColorSpace::SRGB
			? m_SRGBTexturePreviews
			: m_LinearTexturePreviews;

		if (auto previewIt = previewCache.find(textureAsset); previewIt != previewCache.end())
			return previewIt->second;

		Ref<TextureAsset> texture = AssetManager::GetAsset<TextureAsset>(textureAsset);
		if (!texture || !texture->Loaded())
		{
			NV_CORE_WARN("[MaterialPanel] Texture asset {0} is unavailable for preview", (uint64_t)textureAsset);
			return m_CheckerboardTex.IsValid() ? m_CheckerboardTex : RendererResources::GetWhiteTexture();
		}

		Handle<Texture2D> preview = ResourceManager::CreateTexture2D(texture, colorSpace);
		if (!preview.IsValid())
		{
			NV_CORE_WARN("[MaterialPanel] Failed to create preview for texture asset {0}", (uint64_t)textureAsset);
			return m_CheckerboardTex.IsValid() ? m_CheckerboardTex : RendererResources::GetWhiteTexture();
		}

		previewCache[textureAsset] = preview;
		return preview;
	}

	MaterialPanel::TextureSlotEditResult MaterialPanel::DrawTextureSlot(
		const char* id,
		AssetHandle textureAsset,
		TextureColorSpace colorSpace)
	{
		TextureSlotEditResult result{ false, textureAsset };
		ImGui::PushID(id);

		const ImVec2 textureCursorPos = ImGui::GetCursorPos();
		const Handle<Texture2D> preview = GetTexturePreview(textureAsset, colorSpace);
		ui::Image(preview, ImVec2(128.0f, 128.0f));
		const bool imageHovered = ImGui::IsItemHovered();

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("asset_payload"))
			{
				const AssetHandle droppedAsset = *(const AssetHandle*)payload->Data;
				Ref<TextureAsset> texture = AssetManager::GetAsset<TextureAsset>(droppedAsset);
				if (texture && texture->Loaded())
				{
					result.Changed = true;
					result.TextureAsset = droppedAsset;
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (imageHovered && textureAsset != 0 && AssetManager::IsAssetHandleValid(textureAsset))
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(AssetManager::GetMetadata(textureAsset).FilePath.string().c_str());
			ui::Image(preview, ImVec2(256.0f, 256.0f));
			ImGui::EndTooltip();
		}

		const ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
		if (textureAsset != 0)
		{
			ImGui::SetCursorPos(textureCursorPos);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
			if (ImGui::Button("X", ImVec2(18.0f, 18.0f)))
			{
				result.Changed = true;
				result.TextureAsset = 0;
			}
			ImGui::PopStyleVar();
			ImGui::SetCursorPos(nextRowCursorPos);
		}

		ImGui::PopID();
		return result;
	}

	void MaterialPanel::DestroyTexturePreviews()
	{
		auto destroyCache = [](auto& previewCache)
		{
			for (auto& [assetHandle, preview] : previewCache)
			{
				(void)assetHandle;
				if (preview.IsValid())
					ResourceManager::DestroyTexture2D(preview);
			}
			previewCache.clear();
		};

		destroyCache(m_SRGBTexturePreviews);
		destroyCache(m_LinearTexturePreviews);
		m_CheckerboardTex = {};
	}

	void MaterialPanel::CommitChanges()
	{
		if (!m_IsDirty || !m_Material)
			return;

		AssetImporter::Serialize(m_Material);
		m_IsDirty = false;
	}

	void MaterialPanel::Render()
	{
		if (!m_Material)
		{
			ImGui::TextDisabled("No material asset selected.");
			return;
		}

		const AssetMetadata& metadata = AssetManager::GetMetadata(m_Material);
		ImGui::Text("Material: %s", metadata.FilePath.stem().string().c_str());
		if (m_Material->GetMaterial() && m_Material->GetMaterial()->GetShader())
			ImGui::Text("Shader: %s", m_Material->GetMaterial()->GetShader()->GetName().c_str());

		bool materialChanged = false;
		bool commitChanges = false;

		const ImU32 buttonCol = IM_COL32(56, 56, 56, 200);
		const ImU32 buttonHov = IM_COL32(70, 70, 70, 255);
		const ImU32 buttonAct = IM_COL32(56, 56, 56, 255);

		ui::ScopedColourStack buttonColours(ImGuiCol_Button, buttonCol,
			ImGuiCol_ButtonHovered, buttonHov,
			ImGuiCol_ButtonActive, buttonAct);

		// Albedo
		if (ui::PropertyGridHeader("Albedo", true))
		{
			TextureSlotEditResult textureEdit = DrawTextureSlot("AlbedoTexture", m_Material->GetAlbedoMapHandle(), TextureColorSpace::SRGB);
			if (textureEdit.Changed)
			{
				m_Material->SetAlbedoMap(textureEdit.TextureAsset);
				materialChanged = true;
				commitChanges = true;
			}

			ImGui::SameLine();
			ImGui::BeginGroup();
			glm::vec3 albedoColor = m_Material->GetAlbedoColor();
			if (ImGui::ColorEdit3("Color##Albedo", glm::value_ptr(albedoColor)))
			{
				m_Material->SetAlbedoColor(albedoColor);
				materialChanged = true;
			}
			commitChanges |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::EndGroup();
			ui::EndTreeNode();
		}
		// Normal
		if (ui::PropertyGridHeader("Normal", true))
		{
			TextureSlotEditResult textureEdit = DrawTextureSlot("NormalTexture", m_Material->GetNormalMapHandle(), TextureColorSpace::Linear);
			if (textureEdit.Changed)
			{
				m_Material->SetNormalMap(textureEdit.TextureAsset);
				materialChanged = true;
				commitChanges = true;
			}

			ImGui::SameLine();
			ImGui::BeginGroup();
			bool useNormalMap = m_Material->GetMaterialData().UseNormalMap;
			if (ImGui::Checkbox("Use normal map", &useNormalMap))
			{
				m_Material->SetUseNormalMap(useNormalMap);
				materialChanged = true;
				commitChanges = true;
			}
			ImGui::EndGroup();
			ui::EndTreeNode();
		}
		// Metalness
		if (ui::PropertyGridHeader("Metalness", true))
		{
			TextureSlotEditResult textureEdit = DrawTextureSlot("MetalnessTexture", m_Material->GetMetalnessMapHandle(), TextureColorSpace::Linear);
			if (textureEdit.Changed)
			{
				m_Material->SetMetalnessMap(textureEdit.TextureAsset);
				materialChanged = true;
				commitChanges = true;
			}

			ImGui::SameLine();
			ImGui::BeginGroup();
			float metalness = m_Material->GetMetalness();
			ImGui::SetNextItemWidth(200.0f);
			if (ImGui::SliderFloat("Value##Metalness", &metalness, 0.0f, 1.0f))
			{
				m_Material->SetMetalness(metalness);
				materialChanged = true;
			}
			commitChanges |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::EndGroup();
			ui::EndTreeNode();
		}
		// Rough
		if (ui::PropertyGridHeader("Roughness", true))
		{
			TextureSlotEditResult textureEdit = DrawTextureSlot("RoughnessTexture", m_Material->GetRoughnessMapHandle(), TextureColorSpace::Linear);
			if (textureEdit.Changed)
			{
				m_Material->SetRoughnessMap(textureEdit.TextureAsset);
				materialChanged = true;
				commitChanges = true;
			}

			ImGui::SameLine();
			ImGui::BeginGroup();
			float roughness = m_Material->GetRoughness();
			ImGui::SetNextItemWidth(200.0f);
			if (ImGui::SliderFloat("Value##Roughness", &roughness, 0.0f, 1.0f))
			{
				m_Material->SetRoughness(roughness);
				materialChanged = true;
			}
			commitChanges |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::EndGroup();
			ui::EndTreeNode();
		}

		if (ui::PropertyGridHeader("Emission", true))
		{
			float emission = m_Material->GetEmission();
			ImGui::SetNextItemWidth(200.0f);
			if (ImGui::DragFloat("Intensity##Emission", &emission, 0.05f, 0.0f, 100.0f))
			{
				m_Material->SetEmission(emission);
				materialChanged = true;
			}
			commitChanges |= ImGui::IsItemDeactivatedAfterEdit();
			ui::EndTreeNode();
		}

		if (materialChanged)
		{
			m_IsDirty = true;
			AssetEditorManager::NotifyAssetModified(m_Material);
		}

		if (commitChanges)
			CommitChanges();
	}
	void MaterialPanel::OnClose()
	{
		CommitChanges();
		m_Material = nullptr;
	}
}

