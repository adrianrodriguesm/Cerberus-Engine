#include "pch.h"
#include "Asset/AssetManager.h"
#include "ContentBrowserItem.h"
#include "Core/Input.h"
#include "ImGui/ImGuiUI.h"
#include "imgui/imgui_internal.h"
#include "Utils/StringUtils.h"
#include "Utils/FileSystem.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/AssetEditor.h"
namespace cerberus
{
	static char s_RenameBuffer[MAX_INPUT_BUFFER_LENGTH];

	

	ContentBrowserItem::ContentBrowserItem(ItemType type, AssetHandle id, const std::string& name, const Ref<Texture2D>& icon)
		: m_Type(type), m_ID(id), m_Name(name), m_Icon(icon)
	{
	}

	void ContentBrowserItem::OnRenderBegin()
	{
		ImGui::PushID(&m_ID);
		ImGui::BeginGroup();
	}
	CBItemActionResult ContentBrowserItem::OnRender(float thumbnailSize)
	{
		CBItemActionResult result;

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

		m_CBItemData.EdgeOffset = 4.f;
		m_CBItemData.ThumbnailSize = thumbnailSize;
		m_CBItemData.TextLineHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f + m_CBItemData.EdgeOffset * 2.0f;
		m_CBItemData.InfoPanelHeight = std::max(thumbnailSize * 0.5f, m_CBItemData.TextLineHeight);
		
		m_CBItemData.TopLeft = ImGui::GetCursorScreenPos();
		m_CBItemData.ThumbBottomRight = { m_CBItemData.TopLeft.x + thumbnailSize, m_CBItemData.TopLeft.y + thumbnailSize };
		m_CBItemData.InfoTopLeft = { m_CBItemData.TopLeft.x,				 m_CBItemData.TopLeft.y + thumbnailSize };
		m_CBItemData.BottomRight = { m_CBItemData.TopLeft.x + thumbnailSize, m_CBItemData.TopLeft.y + thumbnailSize + m_CBItemData.InfoPanelHeight };
		
		ImGui::InvisibleButton("##thumbnailButton", ImVec2{ thumbnailSize, thumbnailSize });
		// Fill background
		// ----------------
		RenderBackground(m_CBItemData);
		// Thumbnail - Item image
		// ----------------
		ui::draw::DrawButtonImage(m_Icon, IM_COL32(255, 255, 255, 225),
			IM_COL32(255, 255, 255, 255),
			IM_COL32(255, 255, 255, 255),
			ui::RectExpanded(ui::GetItemRect(), -6.0f, -6.0f));
		/**/
		// Info Panel
		// ----------------
		ui::ShiftCursor(m_CBItemData.EdgeOffset, m_CBItemData.EdgeOffset);
		RenderInfoPanel(m_CBItemData, result);
		ui::ShiftCursor(-m_CBItemData.EdgeOffset, -m_CBItemData.EdgeOffset);

		if (!m_IsRenaming)
		{
			if (Input::IsKeyPressed(Key::F2) && m_IsSelected)
				StartRenaming();
		}

		ImGui::PopStyleVar(); // ItemSpacing

		// End of the Item Group
		// ----------------
		ImGui::EndGroup();

		bool dragging = false;
		if (dragging = ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			m_IsDragging = true;
			ImGui::SetDragDropPayload("asset_payload", &m_ID, sizeof(AssetHandle));
			result.Set(ContentBrowserAction::Selected, true);
			ImGui::EndDragDropSource();
		}

		if (ImGui::IsItemHovered())
		{
			result.Set(ContentBrowserAction::Hovered, true);

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				Activate(result);
		}

		// Popup Menu Item
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
		if (ImGui::BeginPopupContextItem("CBItemContextMenu"))
		{
			//result.Set(ContentBrowserAction::Selected, true);
			OnContextMenuOpen(result);
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		m_IsDragging = dragging;

		return result;
	}

	void ContentBrowserItem::OnRenderEnd()
	{
		ImGui::PopID();
		ImGui::NextColumn();
	}
	void ContentBrowserItem::StartRenaming()
	{
		if (m_IsRenaming)
			return;

		memset(s_RenameBuffer, 0, MAX_INPUT_BUFFER_LENGTH);
		memcpy(s_RenameBuffer, m_Name.c_str(), m_Name.size());
		m_IsRenaming = true;
	}
	void ContentBrowserItem::StopRenaming()
	{
		m_IsRenaming = false;
		memset(s_RenameBuffer, 0, MAX_INPUT_BUFFER_LENGTH);
	}
	void ContentBrowserItem::SetSelected(bool value)
	{
		m_IsSelected = value;
	}
	void ContentBrowserItem::RenameWidget(CBItemActionResult& actionResult)
	{
		ImGui::SetKeyboardFocusHere();
		if (ImGui::InputText("##rename", s_RenameBuffer, MAX_INPUT_BUFFER_LENGTH, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			Rename(s_RenameBuffer);
			m_IsRenaming = false;
			actionResult.Set(ContentBrowserAction::Renamed, true);
		}
	}
	void ContentBrowserItem::Rename(const std::string& newName)
	{
		OnRenamed(newName);
	}
	void ContentBrowserItem::OnContextMenuOpen(CBItemActionResult& actionResult)
	{
		if (ImGui::MenuItem("Rename"))
			actionResult.Set(ContentBrowserAction::StartRenaming, true);
		if (ImGui::MenuItem("Delete"))
			actionResult.Set(ContentBrowserAction::OpenDeleteDialogue, true);

		ImGui::Separator();

		if (ImGui::MenuItem("Show In Explorer"))
			actionResult.Set(ContentBrowserAction::ShowInExplorer, true);
	}

	void ContentBrowserItem::RenderShadow(const ImVec2& topLeft, const ImVec2& bottomRight, bool directory)
	{
		auto* drawList = ImGui::GetWindowDrawList();
		const ImRect itemRect = ui::RectOffset(ImRect(topLeft, bottomRight), 1.0f, 1.0f);
		drawList->AddRect(itemRect.Min, itemRect.Max, colors::theme::propertyField, 6.0f, directory ? 0 : ImDrawFlags_RoundCornersBottom, 2.0f);
	}

	ContentBrowserDirectory::ContentBrowserDirectory(const Ref<DirectoryInfo>& directoryInfo, const Ref<Texture2D>& icon)
		: ContentBrowserItem(ContentBrowserItem::ItemType::Directory, directoryInfo->Handle, directoryInfo->FilePath.filename().string(), icon), m_DirectoryInfo(directoryInfo)
	{
	}

	ContentBrowserDirectory::~ContentBrowserDirectory()
	{
	}

	void ContentBrowserDirectory::Delete()
	{
		bool deleted = FileSystem::DeleteFile(m_DirectoryInfo->FilePath);
		if (!deleted)
		{
			NV_CORE_ERROR("Failed to delete folder {0}", m_DirectoryInfo->FilePath);
			return;
		}

		for (auto asset : m_DirectoryInfo->Assets)
			AssetManager::OnAssetDeleted(asset);
	}

	bool ContentBrowserDirectory::Move(const std::filesystem::path& destination)
	{
		bool wasMoved = FileSystem::MoveFile(FileSystem::GetProjectConfig().AssetDirectory / m_DirectoryInfo->FilePath, FileSystem::GetProjectConfig().AssetDirectory / destination);
		if (!wasMoved)
			return false;

		return true;
	}

	void ContentBrowserDirectory::RenderBackground(const ContentBrowserItem::CBItemData& cbItemData)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly) || m_IsSelected || m_IsSelected)
		{
			// Draw shadow
			RenderShadow(cbItemData.TopLeft, cbItemData.BottomRight, true);

			auto* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(cbItemData.TopLeft, cbItemData.BottomRight, colors::theme::groupHeader, 6.0f);
		}
	}

	void ContentBrowserDirectory::RenderInfoPanel(const ContentBrowserItem::CBItemData& cbItemData, CBItemActionResult& actionResult)
	{
		ImGui::BeginVertical((std::string("InfoPanel") + m_Name).c_str(), ImVec2(cbItemData.ThumbnailSize - cbItemData.EdgeOffset * 2.0f, cbItemData.InfoPanelHeight - cbItemData.EdgeOffset));
		{
			// Centre align directory name
			ImGui::BeginHorizontal(m_Name.c_str(), ImVec2(cbItemData.ThumbnailSize - 2.0f, 0.0f));
			ImGui::Spring();
			{
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (cbItemData.ThumbnailSize - cbItemData.EdgeOffset * 3.0f));
				const float textWidth = std::min(ImGui::CalcTextSize(m_Name.c_str()).x, cbItemData.ThumbnailSize);
				if (m_IsRenaming)
				{
					ImGui::SetNextItemWidth(cbItemData.ThumbnailSize - cbItemData.EdgeOffset * 3.0f);
					RenameWidget(actionResult);
				}
				else
				{
					ImGui::SetNextItemWidth(textWidth);
					ImGui::Text(m_Name.c_str());
				}
				ImGui::PopTextWrapPos();
			}
			ImGui::Spring();
			ImGui::EndHorizontal();

			ImGui::Spring();
		}
		ImGui::EndVertical();
	}

	void ContentBrowserDirectory::Activate(CBItemActionResult& actionResult)
	{
		actionResult.Set(ContentBrowserAction::NavigateToThis, true);
	}

	void ContentBrowserDirectory::OnRenamed(const std::string& newName)
	{
		if (FileSystem::Exists(FileSystem::GetProjectConfig().AssetDirectory / m_DirectoryInfo->FilePath.parent_path() / newName))
		{
			NV_CORE_ERROR("A directory with that name already exists!");
			return;
		}

		FileSystem::Rename(m_DirectoryInfo->FilePath, m_DirectoryInfo->FilePath.parent_path() / newName);
	}

	void ContentBrowserDirectory::UpdateDrop(CBItemActionResult& actionResult)
	{
		if (IsSelected())
			return;

		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("asset_payload");

			if (payload)
			{
				auto& currentItems = ContentBrowserPanel::Get().GetCurrentItems();
				uint32_t count = payload->DataSize / sizeof(AssetHandle);

				for (uint32_t i = 0; i < count; i++)
				{
					AssetHandle assetHandle = *(((AssetHandle*)payload->Data) + i);
					size_t index = currentItems.FindItem(assetHandle);
					if (index != ContentBrowserItemList::InvalidItem)
					{
						Ref<ContentBrowserItem> item = currentItems[index];
						if (item->Move(m_DirectoryInfo->FilePath))
						{
							actionResult.Set(ContentBrowserAction::Refresh, true);
							currentItems.Erase(assetHandle);
						}
					}
				}
			}

			ImGui::EndDragDropTarget();
		}
	}

	ContentBrowserAsset::ContentBrowserAsset(const AssetMetadata& assetInfo, const Ref<Texture2D>& icon)
		: ContentBrowserItem(ContentBrowserItem::ItemType::Asset, assetInfo.Handle, assetInfo.FilePath.stem().string(), icon), m_AssetInfo(assetInfo)
	{
	}

	ContentBrowserAsset::~ContentBrowserAsset()
	{

	}

	void ContentBrowserAsset::Delete()
	{
		auto filepath = m_AssetInfo.FilePath;
		bool deleted = FileSystem::DeleteFile(filepath);
		if (!deleted)
		{
			NV_CORE_ERROR("Couldn't delete {0}", m_AssetInfo.FilePath);
			return;
		}

		Ref<DirectoryInfo> currentDirectory = ContentBrowserPanel::Get().GetDirectoryInfo(m_AssetInfo.FilePath.parent_path());
		currentDirectory->Assets.erase(std::remove(currentDirectory->Assets.begin(), currentDirectory->Assets.end(), m_AssetInfo.Handle), currentDirectory->Assets.end());

		AssetManager::OnAssetDeleted(m_AssetInfo.Handle);
	}

	bool ContentBrowserAsset::Move(const std::filesystem::path& destination)
	{
		auto filepath = m_AssetInfo.FilePath;
		bool wasMoved = FileSystem::MoveFile(filepath, FileSystem::GetProjectConfig().AssetDirectory / destination);
		if (!wasMoved)
		{
			NV_CORE_ERROR("Couldn't move {0} to {1}", m_AssetInfo.FilePath, destination);
			return false;
		}

		AssetManager::OnAssetMoved(m_AssetInfo.Handle, destination);
		return true;
	}

	void ContentBrowserAsset::RenderBackground(const ContentBrowserItem::CBItemData& cbItemData)
	{
		
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly) || m_IsSelected)
		//if (ImGui::ItemHoverable(ImRect(cbItemData.TopLeft, cbItemData.BottomRight), ImGui::GetID(&m_ID)) || m_IsSelected)
		{
			// Draw shadow
			RenderShadow(cbItemData.TopLeft, cbItemData.BottomRight, true);
			auto* drawList = ImGui::GetWindowDrawList();
			//drawList->AddRectFilled(cbItemData.TopLeft, cbItemData.ThumbBottomRight, colors::theme::backgroundDark);
			drawList->AddRectFilled(cbItemData.TopLeft, cbItemData.BottomRight, colors::theme::groupHeader, 6.0f);
		}
	}

	void ContentBrowserAsset::RenderInfoPanel(const ContentBrowserItem::CBItemData& cbItemData, CBItemActionResult& actionResult)
	{
		ImGui::BeginVertical((std::string("InfoPanel") + m_Name).c_str(), ImVec2(cbItemData.ThumbnailSize - cbItemData.EdgeOffset * 3.0f, cbItemData.InfoPanelHeight - cbItemData.EdgeOffset));
		{
			//ImGui::BeginHorizontal("label", ImVec2(0.0f, 0.0f));
			ImGui::BeginHorizontal(m_Name.c_str(), ImVec2(cbItemData.ThumbnailSize - 2.0f, 0.0f));
			ImGui::Spring();
			{
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (cbItemData.ThumbnailSize - cbItemData.EdgeOffset * 3.0f));
				const float textWidth = std::min(ImGui::CalcTextSize(m_Name.c_str()).x, cbItemData.ThumbnailSize);
				if (m_IsRenaming)
				{
					ImGui::SetNextItemWidth(cbItemData.ThumbnailSize - cbItemData.EdgeOffset * 3.0f);
					RenameWidget(actionResult);
				}
				else
				{
					ImGui::SetNextItemWidth(textWidth);
					ImGui::Text(m_Name.c_str());
				}
				ImGui::PopTextWrapPos();
			}
			ImGui::Spring();
			ImGui::EndHorizontal();
		}
		ImGui::Spring();
		ImGui::EndVertical();
	}



	void ContentBrowserAsset::Activate(CBItemActionResult& actionResult)
	{
		AssetEditorManager::OpenEditor(AssetManager::GetAsset<Asset>(m_AssetInfo.Handle));
	}

	void ContentBrowserAsset::OnRenamed(const std::string& newName)
	{
		auto filepath = m_AssetInfo.FilePath;
		std::filesystem::path newFilepath = fmt::format("{0}\\{1}{2}", filepath.parent_path().string(), newName, filepath.extension().string());

		if (FileSystem::Rename(filepath, newFilepath))
		{
			// Update AssetManager with new name
			auto& metadata = AssetManager::GetMetadata(m_AssetInfo.Handle);
			AssetManager::OnAssetRenamed(m_AssetInfo.Handle, newFilepath);
		}
		else
		{
			NV_CORE_ERROR("A file with that name already exists!");
		}
	}


}