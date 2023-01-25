#include "pch.h"
#include "DefaultAssetEditorPanels.h"
#include "ImGui/ImGuiUI.h"
#include "Utils/FileSystem.h"
#include "Asset/AssetManager.h"
#include "Renderer/Renderer.h"
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
		if (ui::BeginTreeNode("Materials"))
		{
			ui::BeginPropertyGrid();

			auto& materials = m_MeshSource->GetMaterialsAsset();

			for (size_t i = 0; i < materials.size(); i++)
			{
				std::string label = fmt::format("[Material {0}]", i);
				auto& materialAssetHandle = materials[i]->Handle;
				ui::PropertyAssetReferenceTarget<MaterialAsset>(label.c_str(), materialAssetHandle);
			}

			ui::EndPropertyGrid();
			ui::EndTreeNode();
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
		TextureProperties prop;
		prop.GenerateMips = true;
		prop.SamplerFilter = SamplerFilter::Nearest;
		m_CheckerboardTex = CreateRef<Texture2D>("Resources/Editor/Checkerboard.png", prop);
	}
	MaterialPanel::~MaterialPanel()
	{
		
	}
	void MaterialPanel::Shutdown()
	{
		m_CheckerboardTex->GetImage()->Release();
	}
	void MaterialPanel::SetAsset(const Ref<Asset>& asset)
	{
		m_Material = std::dynamic_pointer_cast<MaterialAsset>(asset);
	}



	void MaterialPanel::Render()
	{
		const auto& material = m_Material->GetMaterial();
		ImGui::Text("Shader: %s", material->GetShader()->GetName().c_str());
		bool needsSerialize = false;

		const ImU32 buttonCol = IM_COL32(56, 56, 56, 200);
		const ImU32 buttonHov = IM_COL32(70, 70, 70, 255);
		const ImU32 buttonAct = IM_COL32(56, 56, 56, 255);

		ui::ScopedColourStack buttonColours(ImGuiCol_Button, buttonCol,
			ImGuiCol_ButtonHovered, buttonHov,
			ImGuiCol_ButtonActive, buttonAct);

		// Albedo
		if (ui::PropertyGridHeader("Albedo", true))
		{
			auto& albedoColor = m_Material->GetAlbedoColor();
			bool hasAlbedoMap = m_Material->IsUsingAlbedoMap();
			Ref<Texture2D> albedoTex = hasAlbedoMap ? m_Material->GetAlbedoMap() : m_CheckerboardTex;

			ImVec2 textureCursorPos = ImGui::GetCursorPos();
			ui::Image(albedoTex, ImVec2(128, 128));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* data = ImGui::AcceptDragDropPayload("asset_payload"))
				{
					AssetHandle assetHandle = *(AssetHandle*)data->Data;
					Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(assetHandle);
					if (texture && texture->GetAssetType() == Texture::GetStaticType())
					{
						m_Material->SetAlbedoMap(texture);
						needsSerialize = true;
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (ImGui::IsItemHovered() && hasAlbedoMap)
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(albedoTex->GetPath().c_str());
				ImGui::PopTextWrapPos();
				ui::Image(albedoTex, ImVec2(256, 256));
				ImGui::EndTooltip();
			}

			ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
			ImGui::SameLine();
			ImVec2 properCursorPos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(textureCursorPos);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));

			if (hasAlbedoMap && ImGui::Button("X", ImVec2(18, 18)))
			{
				m_Material->ClearAlbedoMap();
				needsSerialize = true;
			}
			ImGui::PopStyleVar();
			ImGui::SetCursorPos(properCursorPos);

			ImGui::ColorEdit3("Color##Albedo", glm::value_ptr(albedoColor), ImGuiColorEditFlags_NoInputs);
			if (ImGui::IsItemDeactivated())
				needsSerialize = true;

			ImGui::SetCursorPos(nextRowCursorPos);
			ui::EndTreeNode();
		}
		// Normal
		if (ui::PropertyGridHeader("Normal", true))
		{
			bool hasNormalMap = m_Material->IsUsingNormalMap();
			Ref<Texture2D> normalTex = m_Material->GetNormalMap();
			normalTex = normalTex != Renderer::GetWhiteTexture() ? normalTex : m_CheckerboardTex;

			ImVec2 textureCursorPos = ImGui::GetCursorPos();
			ui::Image(normalTex, ImVec2(128, 128));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* data = ImGui::AcceptDragDropPayload("asset_payload"))
				{
					AssetHandle assetHandle = *(AssetHandle*)data->Data;
					Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(assetHandle);
					if (texture && texture->GetAssetType() == Texture::GetStaticType())
					{
						m_Material->SetNormalMap(texture);
						needsSerialize = true;
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (ImGui::IsItemHovered() && hasNormalMap)
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(normalTex->GetPath().c_str());
				ImGui::PopTextWrapPos();
				ui::Image(normalTex, ImVec2(256, 256));
				ImGui::EndTooltip();
			}

			ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
			ImGui::SameLine();
			ImVec2 properCursorPos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(textureCursorPos);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
			if (hasNormalMap && ImGui::Button("X", ImVec2(18, 18)))
			{
				m_Material->ClearNormalMap();
				needsSerialize = true;
			}
			ImGui::PopStyleVar();
			ImGui::SetCursorPos(properCursorPos);

			
			if (ImGui::Checkbox("Use##Normal", &hasNormalMap))
				m_Material->SetUseNormalMap(hasNormalMap);

			if (ImGui::IsItemDeactivated())
				needsSerialize = true;

			ImGui::SetCursorPos(nextRowCursorPos);
			ui::EndTreeNode();
		}
		// Metalness
		if (ui::PropertyGridHeader("Metalness", true))
		{
			auto& metalness = m_Material->GetMetalness();
			bool hasMetalnessMap = m_Material->IsUsingMetalnessMap();
			Ref<Texture2D> metalnessTex = hasMetalnessMap ? m_Material->GetMetalnessMap() : m_CheckerboardTex;

			ImVec2 textureCursorPos = ImGui::GetCursorPos();
			ui::Image(metalnessTex, ImVec2(128, 128));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* data = ImGui::AcceptDragDropPayload("asset_payload"))
				{
					AssetHandle assetHandle = *(AssetHandle*)data->Data;
					Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(assetHandle);
					if (texture && texture->GetAssetType() == Texture::GetStaticType())
					{
						m_Material->SetMetalnessMap(texture);
						needsSerialize = true;
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (ImGui::IsItemHovered() && hasMetalnessMap)
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(metalnessTex->GetPath().c_str());
				ImGui::PopTextWrapPos();
				ui::Image(metalnessTex, ImVec2(256, 256));
				ImGui::EndTooltip();
			}

			ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
			ImGui::SameLine();
			ImVec2 properCursorPos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(textureCursorPos);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
			if (hasMetalnessMap && ImGui::Button("X", ImVec2(18, 18)))
			{
				m_Material->ClearMetalnessMap();
				needsSerialize = true;
			}
			ImGui::PopStyleVar();
			ImGui::SetCursorPos(properCursorPos);
			ImGui::SetNextItemWidth(200.f);
			ImGui::SliderFloat("Metalness##MetalnessInput", &metalness, 0.0f, 1.0f);
			if (ImGui::IsItemDeactivated())
				needsSerialize = true;

			ImGui::SetCursorPos(nextRowCursorPos);
			ui::EndTreeNode();
		}
		// Rough
		if (ui::PropertyGridHeader("Roughness", true))
		{
			auto& roughness = m_Material->GetRoughness();
			bool hasRoughnessMap = m_Material->IsUsingRoughnessMap();
			Ref<Texture2D> roughnessTex = hasRoughnessMap ? m_Material->GetRoughnessMap() : m_CheckerboardTex;

			ImVec2 textureCursorPos = ImGui::GetCursorPos();
			ui::Image(roughnessTex, ImVec2(128, 128));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* data = ImGui::AcceptDragDropPayload("asset_payload"))
				{
					AssetHandle assetHandle = *(AssetHandle*)data->Data;
					Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(assetHandle);
					if (texture && texture->GetAssetType() == Texture::GetStaticType())
					{
						m_Material->SetRoughnessMap(texture);
						needsSerialize = true;
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (ImGui::IsItemHovered() && hasRoughnessMap)
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(roughnessTex->GetPath().c_str());
				ImGui::PopTextWrapPos();
				ui::Image(roughnessTex, ImVec2(256, 256));
				ImGui::EndTooltip();
			}

			ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
			ImGui::SameLine();
			ImVec2 properCursorPos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(textureCursorPos);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
			if (hasRoughnessMap && ImGui::Button("X", ImVec2(18, 18)))
			{
				m_Material->ClearRoughnessMap();
				needsSerialize = true;
			}
			ImGui::PopStyleVar();
			ImGui::SetCursorPos(properCursorPos);
			ImGui::SetNextItemWidth(200.f);
			ImGui::SliderFloat("Roughness##RoughnessInput", &roughness, 0.0f, 1.0f);
			if (ImGui::IsItemDeactivated())
				needsSerialize = true;

			ImGui::SetCursorPos(nextRowCursorPos);
			ui::EndTreeNode();
		}

		if (needsSerialize)
		{
			NV_CORE_WARN("Serializing...");
			AssetImporter::Serialize(m_Material);
		}

	}
	void MaterialPanel::OnClose()
	{
		m_Material = nullptr;
	}
}

