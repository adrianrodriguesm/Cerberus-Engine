#pragma once
#include "Asset/Asset.h"
namespace cerberus
{
	class AssetEditorPanel
	{
	protected:
		AssetEditorPanel(const std::string& tittle);
	public:
		virtual ~AssetEditorPanel() = default;
		virtual void Shutdown() = 0;
		void OnImGuiRender();
		void SetOpen(bool isOpen) { m_IsOpen = isOpen; }
		virtual void SetAsset(const Ref<Asset>& asset) = 0;
	protected:
		virtual void OnClose() = 0;
		virtual void Render() = 0;
	private:
		std::string m_Title;
		bool m_IsOpen;
	};

	class AssetEditorManager
	{
	public:
		static void Init();
		static void Shutdown();
		static void OnImGuiRender();
		static void OpenEditor(const Ref<Asset>& asset);
	private:
		static std::unordered_map<AssetType, Scope<AssetEditorPanel>> s_AssetEditorPanels;
	};
}