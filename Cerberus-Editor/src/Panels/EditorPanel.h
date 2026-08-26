#pragma once
#include "Scene/Entity.h"
#include "Core/Base.h"
namespace cerberus
{
	class EditorPanel
	{
	public:
		virtual ~EditorPanel() = default;

		virtual void Shutdown() = 0;
		virtual void OnImGuiRender(bool& isOpen) = 0;
		virtual void SetSceneContext(const Ref<Scene>& context) {}
		virtual void SetSelectionContext(const std::vector<Entity>& context) {}
	};

	struct PanelData
	{
		bool IsOpen = false;
		Ref<EditorPanel> Panel = nullptr;
	};

	class EditorPanelManager
	{
	public:
		EditorPanelManager() = default;
		~EditorPanelManager()
		{
			m_Panels.clear();
		}

		void Shutdown()
		{
			for (auto& [id, panelData] : m_Panels)
				panelData.Panel->Shutdown();

			m_Panels.clear();
		}

		void OnImGuiRender()
		{
			for (auto& [id, panelData] : m_Panels)
			{
				if (panelData.IsOpen)
					panelData.Panel->OnImGuiRender(panelData.IsOpen);
			}
		}

		void SetSceneContext(const Ref<Scene>& context)
		{
			for (auto& [id, panelData] : m_Panels)
				panelData.Panel->SetSceneContext(context);
		}


		void SetSelectionContext(const std::vector<Entity>& context)
		{
			for (auto& [id, panelData] : m_Panels)
				panelData.Panel->SetSelectionContext(context);
		}



		void RemovePanel(const std::string& strID)
		{
			if (m_Panels.find(strID) == m_Panels.end())
			{
				NV_CORE_ERROR("[PanelManager]: Couldn't find panel with id '{0}'", strID);
				return;
			}

			m_Panels.erase(strID);
		}

		void AddPanel(const std::string& strID, const PanelData& paneData)
		{
			if (m_Panels.find(strID) != m_Panels.end())
			{
				NV_CORE_ERROR("[PanelManager]: A panel with the id '{0}' has already been added.", strID);
				return;
			}
			m_Panels[strID] = paneData;
		}

		template<typename TPanel>
		Ref<TPanel> GetPanel(const std::string& strID)
		{
			static_assert(std::is_base_of<EditorPanel, TPanel>::value, "PanelManager::AddPanel requires TPanel to inherit from EditorPanel");
			if (m_Panels.find(strID) == m_Panels.end())
			{
				NV_CORE_ERROR("[PanelManager]: Couldn't find panel with id '{0}'", strID);
				return nullptr;
			}
			return std::dynamic_pointer_cast<TPanel>(m_Panels[strID].Panel);
		}

		std::unordered_map<std::string, PanelData>& GetPanels() { return m_Panels; }
		const std::unordered_map<std::string, PanelData>& GetPanels() const { return m_Panels; }

	private:
		std::unordered_map<std::string, PanelData> m_Panels;
	};
}