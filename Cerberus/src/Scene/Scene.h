#pragma once
#include "Core/Timestep.h"
#include "Scene/SceneEnvironment.h"
#include <entt.hpp>
#include <glm/glm.hpp>
#include "Asset/Asset.h"
namespace cerberus
{
	struct SceneRendererCamera;
	class SceneRenderer;
	class Entity;
	class SceneHierarchyPanel;
	class ScenePropertiesPanel;
	class SceneSerializer;

	class Scene : public Asset
	{
	public:
		Scene(const std::string& name = "Untitled Scene");
		~Scene();

		void OnUpdate(Ref<SceneRenderer>  sceneRenderer, const SceneRendererCamera& sceneRendererCamera, Timestep ts);

		//void OnViewportResize(uint32_t width, uint32_t height);
		void SetViewport(uint32_t width, uint32_t height);

		Entity CreateEntity(const std::string& tagName = "Entity");
		void DestroyEntity(Entity entity);

		void SetName(const std::string& name) { m_Name = name; }

		inline glm::vec2 GetViewportSize() const { return glm::vec2(m_Width, m_Heigth); }

		static AssetType GetStaticType() { return AssetType::Scene; }
		AssetType GetAssetType() const { return GetStaticType(); }
	private:
		entt::registry m_Registry;
		uint32_t m_Width, m_Heigth;
		std::string m_Name;

		LightEnvironment m_LightEnvironment;

		friend class Entity;
		friend class SceneRenderer;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;
		friend class ScenePropertiesPanel;
	};
}


