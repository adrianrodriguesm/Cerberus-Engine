#pragma once
#include "EditorPanel.h"
#include "Renderer/Texture.h"
#include "ContentBrowser/ContentBrowserItem.h"
namespace cerberus
{
	struct ContentBrowserItemList
	{
		static constexpr size_t InvalidItem = std::numeric_limits<size_t>::max();

		std::vector<Ref<ContentBrowserItem>> Items;

		std::vector<Ref<ContentBrowserItem>>::iterator begin() { return Items.begin(); }
		std::vector<Ref<ContentBrowserItem>>::iterator end() { return Items.end(); }
		std::vector<Ref<ContentBrowserItem>>::const_iterator begin() const { return Items.begin(); }
		std::vector<Ref<ContentBrowserItem>>::const_iterator end() const { return Items.end(); }

		Ref<ContentBrowserItem>& operator[](size_t index) { return Items[index]; }
		const Ref<ContentBrowserItem>& operator[](size_t index) const { return Items[index]; }

		void Clear()
		{
			Items.clear();
		}

		void Erase(AssetHandle handle)
		{
			size_t index = FindItem(handle);
			if (index == InvalidItem)
				return;

			auto it = Items.begin() + index;
			Items.erase(it);
		}

		size_t FindItem(AssetHandle handle) const
		{
			for (size_t i = 0; i < Items.size(); i++)
			{
				if (Items[i]->GetID() == handle)
					return i;
			}

			return InvalidItem;
		}

		Ref<ContentBrowserItem> TryGetSelectedItem()
		{
			for (size_t i = 0; i < Items.size(); i++)
			{
				if (Items[i]->IsSelected())
					return Items[i];
			}
			return nullptr;
		}
	};

	class ContentBrowserPanel : public EditorPanel
	{
	public:
		ContentBrowserPanel();
		virtual ~ContentBrowserPanel();

		virtual void Shutdown() override;
		virtual void OnImGuiRender(bool& isOpen) override;
		virtual void SetSceneContext(const Ref<Scene>& context) {}
		virtual void SetSelectionContext(const Entity& context) {}

		ContentBrowserItemList& GetCurrentItems() { return m_CurrentItemList; }
		void Refresh();
		bool FocusAsset(AssetHandle assetHandle);

		static ContentBrowserPanel& Get() { return *s_Instance; }
	private:
		Handle<Texture2D> LoadIcon(const std::filesystem::path& filepath);
		void UpdateInput();
		void RenderItems();
		void RenderTopBar(float height);
		void RenderPopupMenu();
		void RenderDeletePopupMenu();
		// Define the BaseDirectory and fill m_Directories
		AssetHandle ProcessDirectory(const std::filesystem::path& directoryPath, const Ref<DirectoryInfo>& parent = nullptr);
		// Get the directory info from m_Directories. Return a Ref<DirectoryInfo> if succeed or nullptr otherwise
		Ref<DirectoryInfo> GetDirectoryInfo(const std::filesystem::path& directoryPath);
		// Update m_Current directory base on directory parameter and fill m_ItemList base on that
		void ChangeDirectory(Ref<DirectoryInfo>& directory);
		void SortItemList();
		void ClearSelections();
	private:
		template<typename T, typename... Args>
		Ref<T> CreateAsset(const std::string& filename, Args&&... args)
		{
			Ref<T> asset = AssetManager::CreateNewAsset<T>(filename, m_CurrentDirectory->FilePath.string(), std::forward<Args>(args)...);
			if (!asset)
				return nullptr;

			m_CurrentDirectory->Assets.push_back(asset->Handle);
			ChangeDirectory(m_CurrentDirectory);

			auto& item = m_CurrentItemList[m_CurrentItemList.FindItem(asset->Handle)];
			item->SetSelected(true);
			item->StartRenaming();

			return asset;
		}
	private:
		Handle<Texture2D> m_FileIcon, m_DirectoryIcon, m_RefreshButtonIcon, m_BackButtonIcon;
		Ref<DirectoryInfo> m_BaseDirectory; // AssetDirectory
		Ref<DirectoryInfo> m_CurrentDirectory;
		std::unordered_map<AssetHandle, Ref<DirectoryInfo>> m_Directories;
		ContentBrowserItemList m_CurrentItemList;
		std::unordered_map<std::string, Handle<Texture2D>> m_IconMap;
		bool m_IsAnyItemHovered;
		AssetHandle m_PendingScrollToAsset = 0;
		bool m_RequestWindowFocus = false;
	private:
		friend class ContentBrowserItem;
		friend class ContentBrowserDirectory;
		friend class ContentBrowserAsset;
	private:
		static ContentBrowserPanel* s_Instance;
	};
}
