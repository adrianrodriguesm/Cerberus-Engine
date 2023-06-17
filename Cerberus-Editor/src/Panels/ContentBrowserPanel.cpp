#include "pch.h"
#include "ContentBrowserPanel.h"
#include "ImGui/ImGuiUI.h"
#include "Utils/FileSystem.h"
#include "Utils/StringUtils.h"
#include "Core/Input.h"
#include "Utils/FileDialogs.h"
namespace cerberus
{
	ContentBrowserPanel* ContentBrowserPanel::s_Instance;

	static const std::filesystem::path s_AssetDirectory = std::filesystem::path("Resources");

	ContentBrowserPanel::ContentBrowserPanel()
	{
		s_Instance = this;
#if OLD_RENDERER
		TextureProperties texProp;
		texProp.SamplerWrap = SamplerWrap::Clamp;
		m_DirectoryIcon = CreateRef<Texture2D>("Resources/Editor/Folder.png", texProp);
		m_FileIcon = CreateRef<Texture2D>("Resources/Editor/File.png", texProp);
		m_BackButtonIcon = CreateRef<Texture2D>("Resources/Editor/Back.png", texProp);
		m_RefreshButtonIcon = CreateRef<Texture2D>("Resources/Editor/Refresh.png", texProp);

		m_IconMap[".fbx"] =		  CreateRef<Texture2D>("Resources/Editor/FBX-Icon.png", texProp);
		m_IconMap[".obj"] =		  CreateRef<Texture2D>("Resources/Editor/OBJ-Icon.png", texProp);
		m_IconMap[".gltf"] =	  CreateRef<Texture2D>("Resources/Editor/GLTF-Icon.png", texProp);
		m_IconMap[".png"] =		  CreateRef<Texture2D>("Resources/Editor/PNG-Icon.png", texProp);
		m_IconMap[".jpg"] =		  CreateRef<Texture2D>("Resources/Editor/JPG-Icon.png", texProp);
		m_IconMap[".jpeg"] =	  CreateRef<Texture2D>("Resources/Editor/JPEG-Icon.png", texProp);
		m_IconMap[".hdr"] =		  CreateRef<Texture2D>("Resources/Editor/HDR-Icon.png", texProp);
		m_IconMap[".cmaterial"] = CreateRef<Texture2D>("Resources/Editor/Material-Icon.png", texProp);
		m_IconMap[".cscene"] =    CreateRef<Texture2D>("Resources/Editor/Scene-Icon.png", texProp);
		m_IconMap[".cmesh"] =	  CreateRef<Texture2D>("Resources/Editor/Mesh-Icon.png", texProp);

		AssetHandle baseDirectoryHandle = ProcessDirectory(s_AssetDirectory, nullptr);
		m_BaseDirectory = m_Directories[baseDirectoryHandle];
		ChangeDirectory(m_BaseDirectory);
#endif
	}

	ContentBrowserPanel::~ContentBrowserPanel()
	{
	}

	void ContentBrowserPanel::Shutdown()
	{
		m_CurrentItemList.Clear();
		m_IconMap.clear();
		m_Directories.clear();
#if OLD_RENDERER
		m_DirectoryIcon->GetImage()->Release();
		m_FileIcon->GetImage()->Release();
		m_BackButtonIcon->GetImage()->Release();
		m_RefreshButtonIcon->GetImage()->Release();
#endif
	}

	void ContentBrowserPanel::OnImGuiRender(bool& isOpen)
	{
		ImGui::Begin("Content Browser", &isOpen, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
		{
			ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable
				| ImGuiTableFlags_SizingFixedFit
				| ImGuiTableFlags_BordersInnerV;

			ui::PushID();
			if (ImGui::BeginTable(ui::GenerateID(), 1, tableFlags, ImVec2(0.0f, 0.0f)))
			{
				ImGui::TableSetupColumn("Directory Structure", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				// Directory Content
				const float topBarHeight = 26.0f;
				const float bottomBarHeight = 52.0f;
				ImGui::BeginChild("##directory_structure", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetWindowHeight() - topBarHeight - bottomBarHeight));
				{
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
					RenderTopBar(topBarHeight);
					ImGui::PopStyleVar();
					ImGui::Separator();

					ImGui::BeginChild("Scrolling");
					{
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.35f));
						RenderPopupMenu();
						RenderItems();

						if (ImGui::IsWindowFocused() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
							UpdateInput();

						ImGui::PopStyleColor(2);
						RenderDeletePopupMenu();
					}
					
					ImGui::EndChild();
				}
				ImGui::EndChild();
				ImGui::EndTable();
			}
			ui::PopID();
		}
		ImGui::End();
	}
	void ContentBrowserPanel::Refresh()
	{
		m_CurrentItemList.Clear();
		m_Directories.clear();

		Ref<DirectoryInfo> currentDirectory = m_CurrentDirectory;
		AssetHandle baseDirectoryHandle = ProcessDirectory(s_AssetDirectory, nullptr);
		m_BaseDirectory = m_Directories[baseDirectoryHandle];
		m_CurrentDirectory = GetDirectoryInfo(currentDirectory->FilePath);
		// Our directory was removed
		if (!m_CurrentDirectory)
			m_CurrentDirectory = m_BaseDirectory;

		ChangeDirectory(m_CurrentDirectory);
	}
	void ContentBrowserPanel::UpdateInput()
	{
		if(!ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))
			return;

		if ((!m_IsAnyItemHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) || Input::IsKeyPressed(Key::Escape))
			ClearSelections();

		if (Input::IsKeyPressed(Key::F5))
			Refresh();

	}
	void ContentBrowserPanel::RenderItems()
	{
		m_IsAnyItemHovered = false;
		bool deleteItem = false;
		static float s_Padding = 2.f;
		static float s_ThumbnailSize = 128.f;
		const float paddingForOutline = 2.0f;
		float cellSize = s_ThumbnailSize + s_Padding + paddingForOutline; // 274 pixels per cell

		const float scrollBarrOffset = 20.0f + ImGui::GetStyle().ScrollbarSize;
		float panelWidth = ImGui::GetContentRegionAvail().x - scrollBarrOffset;

		int columnCount = (int)(panelWidth / cellSize);
		columnCount = columnCount < 1 ? 1 : columnCount;
		const float rowSpacing = 12.0f;
		ui::ScopedStyle spacing(ImGuiStyleVar_ItemSpacing, ImVec2(paddingForOutline, rowSpacing));
		ImGui::Columns(columnCount, 0, false);

		ui::ScopedStyle border(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ui::ScopedStyle padding(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
		ui::ScopedColor scopedColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		// Render actions
		for (auto& item : m_CurrentItemList)
		{
			item->OnRenderBegin();
			CBItemActionResult actionResult = item->OnRender(s_ThumbnailSize);
			item->OnRenderEnd();
			// Execute action
			{
				if (actionResult.IsSet(ContentBrowserAction::Hovered))
				{
					m_IsAnyItemHovered = true;
				}
				if (actionResult.IsSet(ContentBrowserAction::ShowInExplorer))
				{
					item->SetSelected(true);
					if (item->GetType() == ContentBrowserItem::ItemType::Directory)
						FileSystem::ShowFileInExplorer(m_CurrentDirectory->FilePath / item->GetName());
					else
						FileSystem::ShowFileInExplorer(AssetManager::GetMetadata(item->GetID()).FilePath);

					continue;
				}
				if (actionResult.IsSet(ContentBrowserAction::StartRenaming))
				{
					item->StartRenaming();
					continue;
				}
				if (actionResult.IsSet(ContentBrowserAction::OpenDeleteDialogue))
				{
					item->SetSelected(true);
					deleteItem = true;
					continue;
				}

				if (actionResult.IsSet(ContentBrowserAction::NavigateToThis))
				{
					ChangeDirectory(std::dynamic_pointer_cast<ContentBrowserDirectory>(item)->GetDirectoryInfo());
					break;
				}
				if (actionResult.IsSet(ContentBrowserAction::Renamed))
				{
					Refresh();
					SortItemList();
					item->SetSelected(true);
					break;
				}

				

			}
		}

		if (deleteItem)
			ImGui::OpenPopup("Delete");
	}
	void ContentBrowserPanel::RenderTopBar(float height)
	{
		ImGui::BeginChild("##top_bar", ImVec2(0, height));
		ImGui::BeginHorizontal("##top_bar", ImGui::GetWindowSize());
		{
			float edgeOffset = 4.f;
			// Navigation buttons
			{
				ui::ScopedStyle spacing(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));

				auto contenBrowserButton = [height](const char* labelId, const Ref<Texture2D>& icon)
				{
					const ImU32 buttonCol = colors::theme::backgroundDark;
					const ImU32 buttonColP = ui::ColourWithMultipliedValue(colors::theme::backgroundDark, 0.8f);
					ui::ScopedColourStack buttonColours(ImGuiCol_Button, buttonCol,
						ImGuiCol_ButtonHovered, buttonCol,
						ImGuiCol_ButtonActive, buttonColP);

					const float iconSize = std::min(24.0f, height);
					const float iconPadding = 3.0f;
					const bool clicked = ImGui::Button(labelId, ImVec2(iconSize, iconSize));
					//ImU32 color = IM_COL32(128, 128, 128, 255);;
					ui::draw::DrawButtonImage(icon, colors::theme::textDarker,
						ui::ColourWithMultipliedValue(colors::theme::textDarker, 1.2f),
						ui::ColourWithMultipliedValue(colors::theme::textDarker, 0.8f),
						ui::RectExpanded(ui::GetItemRect(), -iconPadding, -iconPadding));

					return clicked;
				};
				// Back button
				if (contenBrowserButton("##back", m_BackButtonIcon) && m_CurrentDirectory != m_BaseDirectory)
				{
					ChangeDirectory(m_CurrentDirectory->Parent);
				}
				ImGui::Spring(-1.0f, edgeOffset * 2.0f);
				// Back button
				if (contenBrowserButton("##refresh", m_RefreshButtonIcon))
				{
					Refresh();
				}

			}

		}
		ImGui::EndHorizontal();
		ImGui::EndChild();
	}

	void ContentBrowserPanel::RenderPopupMenu()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
		if (ImGui::BeginPopupContextWindow(0))
		{
			if (ImGui::BeginMenu("New"))
			{
				if (ImGui::MenuItem("Folder"))
				{
					bool created = FileSystem::CreateDirectory(m_CurrentDirectory->FilePath / "New Folder");
					if (created)
					{
						Refresh();
						const auto& directoryInfo = GetDirectoryInfo(m_CurrentDirectory->FilePath / "New Folder");
						size_t index = m_CurrentItemList.FindItem(directoryInfo->Handle);
						if (index != ContentBrowserItemList::InvalidItem)
							m_CurrentItemList[index]->StartRenaming();
					}
				}
				if (ImGui::MenuItem("Scene"))
					CreateAsset<Scene>("New Scene.cscene");

				if (ImGui::MenuItem("Material"))
					CreateAsset<MaterialAsset>("New Material.cmaterial");

				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Refresh"))
				Refresh();

			if (ImGui::MenuItem("Import Asset"))
			{
				std::string filepathString = FileDialogs::OpenFile();
				std::filesystem::path filepath = filepathString;
				if (!filepathString.empty())
					FileSystem::MoveFile(filepath, m_CurrentDirectory->FilePath);

				AssetManager::ImportAsset(filepath);
				Refresh();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Show in Explorer"))
				FileSystem::OpenDirectoryInExplorer(m_CurrentDirectory->FilePath);

			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
	}

	void ContentBrowserPanel::RenderDeletePopupMenu()
	{
		if (ImGui::BeginPopupModal("Delete", NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			Ref<ContentBrowserItem> item = m_CurrentItemList.TryGetSelectedItem();
			if (!item)
			{
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}

			ImGui::Text("Are you sure you want to delete this item?");

			float columnWidth = ImGui::GetContentRegionAvailWidth() / 4;

			ImGui::Columns(4, 0, false);
			ImGui::SetColumnWidth(0, columnWidth);
			ImGui::SetColumnWidth(1, columnWidth);
			ImGui::SetColumnWidth(2, columnWidth);
			ImGui::SetColumnWidth(3, columnWidth);
			ImGui::NextColumn();

			if (ImGui::Button("Yes", ImVec2(columnWidth, 0)))
			{
				item->Delete();
				m_CurrentItemList.Erase(item->GetID());

				Refresh();

				ImGui::CloseCurrentPopup();
			}

			ImGui::NextColumn();
			ImGui::SetItemDefaultFocus();
			if (ImGui::Button("No", ImVec2(columnWidth, 0)))
				ImGui::CloseCurrentPopup();

			ImGui::NextColumn();

			ImGui::EndPopup();
		}
	}

	AssetHandle ContentBrowserPanel::ProcessDirectory(const std::filesystem::path& directoryPath, const Ref<DirectoryInfo>& parent)
	{
		// Check if the directory already exists
		const auto& directory = GetDirectoryInfo(directoryPath);
		if (directory)
			return directory->Handle;

		// If thats no the case then process the filepath in order to create the directory
		Ref<DirectoryInfo> directoryInfo = CreateRef<DirectoryInfo>();
		directoryInfo->Handle = AssetHandle();
		directoryInfo->Parent = parent;

		directoryInfo->FilePath = directoryPath;
		for (auto entry : std::filesystem::directory_iterator(directoryPath))
		{
			// Process the subdirectory in a recursive way
			if (entry.is_directory())
			{
				AssetHandle subdirHandle = ProcessDirectory(entry.path(), directoryInfo);
				directoryInfo->SubDirectories[subdirHandle] = m_Directories[subdirHandle];
				continue;
			}
			// Otherwise the entry is an asset
			//std::filesystem::path path = std::filesystem::relative(entry.path(), s_AssetDirectory);
			AssetMetadata metadata = AssetManager::GetMetadata(entry.path());
			
			if (!metadata.IsValid())
			{
				AssetType type = AssetManager::GetAssetTypeFromPath(entry.path());
				if (type == AssetType::None)
					continue;

				metadata.Handle = AssetManager::ImportAsset(entry.path());
				// If the metadata still is invalid then continue to the next entry
				if (!metadata.IsValid())
					continue;
			}
			// Add the asset to the directory info
			directoryInfo->Assets.push_back(metadata.Handle);
		}

		m_Directories[directoryInfo->Handle] = directoryInfo;
		return directoryInfo->Handle;
	}

	Ref<DirectoryInfo> ContentBrowserPanel::GetDirectoryInfo(const std::filesystem::path& directoryPath)
	{
		// If the filepath if empty then return the base directory (asset root directory)
		if (directoryPath.string() == "")
			return m_BaseDirectory;
		// Otherwise iterate over all the directories and try to find the one that matches the filepath
		for (const auto& [assetHandle, directoryInfo] : m_Directories)
		{
			if (directoryInfo->FilePath == directoryPath)
				return directoryInfo;
		}
		// Return null if there are no matches
		return nullptr;
	}

	void ContentBrowserPanel::ChangeDirectory(Ref<DirectoryInfo>& directory)
	{
		// If the directory is invalid then return
		if (!directory)
			return;

		m_CurrentItemList.Items.clear();
		// Fill the directories
		for (auto& [subdirHandle, subdir] : directory->SubDirectories)
		{
			m_CurrentItemList.Items.push_back(CreateRef<ContentBrowserDirectory>(subdir, m_DirectoryIcon));
		}

		for (auto assetHandle : directory->Assets)
		{
			AssetMetadata metadata = AssetManager::GetMetadata(assetHandle);
			
			if (metadata.IsValid())
			{
				std::string extension = metadata.FilePath.extension().string();
				if(m_IconMap.find(extension) != m_IconMap.end())
					m_CurrentItemList.Items.push_back(CreateRef<ContentBrowserAsset>(metadata, m_IconMap[extension]));
				else
					m_CurrentItemList.Items.push_back(CreateRef<ContentBrowserAsset>(metadata, m_FileIcon));
			}
		}

		SortItemList();
		m_CurrentDirectory = directory;

		ClearSelections();
	}
	void ContentBrowserPanel::SortItemList()
	{
		std::sort(m_CurrentItemList.begin(), m_CurrentItemList.end(), [](const Ref<ContentBrowserItem>& item1, const Ref<ContentBrowserItem>& item2)
		{
			if (item1->GetType() == item2->GetType())
				return utils::ToLower(item1->GetName()) < utils::ToLower(item2->GetName());

			return (uint16_t)item1->GetType() < (uint16_t)item2->GetType();
		});
	}
	void ContentBrowserPanel::ClearSelections()
	{
		for (auto& item : m_CurrentItemList)
		{
			item->SetSelected(false);

			if (item->IsRenaming())
				item->StopRenaming();
		}
	}
}