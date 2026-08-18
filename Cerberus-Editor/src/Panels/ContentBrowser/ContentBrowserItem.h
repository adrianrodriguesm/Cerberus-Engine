#pragma once
#include "Asset/Asset.h"
#include "Renderer/Texture.h"
#include <imgui/imgui.h>
#define MAX_INPUT_BUFFER_LENGTH 128

namespace cerberus
{
	enum class ContentBrowserAction
	{
		None = 0,
		Refresh = BIT(0),
		ClearSelections = BIT(1),
		Selected = BIT(2),
		Hovered = BIT(3),
		Renamed = BIT(4),
		NavigateToThis = BIT(5),
		OpenDeleteDialogue = BIT(6),
		SelectToHere = BIT(7),
		Moved = BIT(8),
		ShowInExplorer = BIT(9),
		OpenExternal = BIT(10),
		Reload = BIT(11),
		Copy = BIT(12),
		Duplicate = BIT(13),
		StartRenaming = BIT(14)
	};

	struct CBItemActionResult
	{
		uint16_t Field = 0;

		void Set(ContentBrowserAction flag, bool value)
		{
			if (value)
				Field |= (uint16_t)flag;
			else
				Field &= ~(uint16_t)flag;
		}

		bool IsSet(ContentBrowserAction flag) const { return (uint16_t)flag & Field; }
	};
	/////////////////////////////////////////////////////////////////////////////
	////// ContentBrowserItem
	/////////////////////////////////////////////////////////////////////////////
	class ContentBrowserItem
	{
	public:
		enum class ItemType : uint16_t
		{
			Directory, Asset
		};
	public:
		ContentBrowserItem(ItemType type, AssetHandle id, const std::string& name, Handle<Texture2D> icon);
		virtual ~ContentBrowserItem() {}

		void OnRenderBegin();
		CBItemActionResult OnRender(float thumbnailSize);
		void OnRenderEnd();

		virtual void Delete() = 0;
		virtual bool Move(const std::filesystem::path& destination) = 0;

		bool IsSelected() const { return m_IsSelected; }

		AssetHandle GetID() const { return m_ID; }
		ItemType GetType() const { return m_Type; }
		const std::string& GetName() const { return m_Name; }
		Handle<Texture2D> GetIcon() const { return m_Icon; }

		virtual void Activate(CBItemActionResult& actionResult) = 0;
		void StartRenaming();
		void StopRenaming();
		bool IsRenaming() const { return m_IsRenaming; }

		void SetSelected(bool value);
	protected:
		struct CBItemData
		{
			float EdgeOffset = 4.0f;
			float ThumbnailSize = 64.f;
			float TextLineHeight;
			float InfoPanelHeight;

			ImVec2 TopLeft;
			ImVec2 ThumbBottomRight;
			ImVec2 InfoTopLeft;
			ImVec2 BottomRight;
		} m_CBItemData;
	private:
		void Rename(const std::string& newName);
		virtual void OnRenamed(const std::string& newName) = 0;
		virtual void UpdateDrop(CBItemActionResult& actionResult) = 0;
		void OnContextMenuOpen(CBItemActionResult& actionResult);
	protected:
		void RenameWidget(CBItemActionResult& actionResult);
		void RenderShadow(const ImVec2& topLeft, const ImVec2& bottomRight, bool directory);
		virtual void RenderBackground(const ContentBrowserItem::CBItemData& cbItemData) = 0;
		virtual void RenderInfoPanel(const ContentBrowserItem::CBItemData& cbItemData, CBItemActionResult& actionResult) = 0;

	protected:
		ItemType m_Type;
		AssetHandle m_ID;
		std::string m_Name;
		Handle<Texture2D> m_Icon;

		bool m_IsSelected = false;
		bool m_IsRenaming = false;
		bool m_IsDragging = false;
	};

	/////////////////////////////////////////////////////////////////////////////
	////// ContentBrowserDirectory
	/////////////////////////////////////////////////////////////////////////////
	struct DirectoryInfo
	{
		AssetHandle Handle;
		Ref<DirectoryInfo> Parent;

		std::filesystem::path FilePath;

		std::vector<AssetHandle> Assets;

		std::map<AssetHandle, Ref<DirectoryInfo>> SubDirectories;
	};

	class ContentBrowserDirectory : public ContentBrowserItem
	{
	public:
		ContentBrowserDirectory(const Ref<DirectoryInfo>& directoryInfo, Handle<Texture2D> icon);
		virtual ~ContentBrowserDirectory();

		Ref<DirectoryInfo>& GetDirectoryInfo() { return m_DirectoryInfo; }

		virtual void Delete() override;
		virtual bool Move(const std::filesystem::path& destination) override;
	private:
		virtual void RenderBackground(const ContentBrowserItem::CBItemData& cbItemData) override;
		virtual void RenderInfoPanel(const ContentBrowserItem::CBItemData& cbItemData, CBItemActionResult& actionResult) override;
		virtual void Activate(CBItemActionResult& actionResult) override;
		virtual void OnRenamed(const std::string& newName) override;
		virtual void UpdateDrop(CBItemActionResult& actionResult) override;
	private:
		Ref<DirectoryInfo> m_DirectoryInfo;
	};
	/////////////////////////////////////////////////////////////////////////////
	////// ContentBrowserAsset
	/////////////////////////////////////////////////////////////////////////////
	class ContentBrowserAsset : public ContentBrowserItem
	{
	public:
		ContentBrowserAsset(const AssetMetadata& assetInfo, Handle<Texture2D> icon);
		virtual ~ContentBrowserAsset();

		const AssetMetadata& GetAssetInfo() const { return m_AssetInfo; }

		virtual void Delete() override;
		virtual bool Move(const std::filesystem::path& destination) override;
	private:
		virtual void RenderBackground(const ContentBrowserItem::CBItemData& cbItemData) override;
		virtual void RenderInfoPanel(const ContentBrowserItem::CBItemData& cbItemData, CBItemActionResult& actionResult) override;

		virtual void Activate(CBItemActionResult& actionResult) override;
		virtual void OnRenamed(const std::string& newName) override;
		virtual void UpdateDrop(CBItemActionResult& actionResult) {};


	private:
		AssetMetadata m_AssetInfo;
	};


	namespace utils
	{
		static std::string ContentBrowserItemTypeToString(ContentBrowserItem::ItemType type)
		{
			switch (type)
			{
			case ContentBrowserItem::ItemType::Asset: return "Asset";
			case ContentBrowserItem::ItemType::Directory: return "Directory";
			}

			return "Unknown";
		}
	}
}
