#include "pch.h"
#include "AssetEditor.h"
#include "ImGui/imgui.h"
#include "DefaultAssetEditorPanels.h"
namespace cerberus
{
	std::unordered_map<AssetType, Scope<AssetEditorPanel>> AssetEditorManager::s_AssetEditorPanels;

	AssetEditorPanel::AssetEditorPanel(const std::string& tittle)
		: m_Title(tittle), m_IsOpen(false)
	{
	}

	void AssetEditorPanel::OnImGuiRender()
	{
		if (!m_IsOpen)
			return;

		bool wasOpen = m_IsOpen;
		ImGui::SetNextWindowSizeConstraints({ 200, 400 }, { 2000, 2000 });
		ImGui::Begin(m_Title.c_str(), &m_IsOpen);
		Render();
		ImGui::End();

		if (!m_IsOpen && wasOpen)
			OnClose();
	}

	void AssetEditorManager::Init()
	{
		s_AssetEditorPanels[AssetType::Material] = CreateScope<MaterialPanel>();
		s_AssetEditorPanels[AssetType::MeshSource] = CreateScope<MeshPanel>();
	}

	void AssetEditorManager::Shutdown()
	{
		for (auto& [assetType, panel] : s_AssetEditorPanels)
			panel->Shutdown();

		s_AssetEditorPanels.clear();
	}

	void AssetEditorManager::OnImGuiRender()
	{
		for (auto& [assetType, panel] : s_AssetEditorPanels)
			panel->OnImGuiRender();
	}

	void AssetEditorManager::OpenEditor(const Ref<Asset>& asset)
	{
		AssetType assetType = asset->GetAssetType();
		if (s_AssetEditorPanels.find(assetType) == s_AssetEditorPanels.end())
			return;

		s_AssetEditorPanels[assetType]->SetAsset(asset);
		s_AssetEditorPanels[assetType]->SetOpen(true);

	}
}