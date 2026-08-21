#pragma once
#include "Core/Timestep.h"
#include "Scene/SceneEnvironment.h"
#include <entt.hpp>
#include <glm/glm.hpp>
#include "Asset/Asset.h"
#include "Components/Components.h"
#include "Renderer/RenderObjectID.h"
#include "Renderer/SceneRenderer/SceneRendererType.h"
namespace cerberus
{
	struct SceneRendererCamera;
	class SceneRenderer;
	class Entity;
	class SceneHierarchyPanel;
	class ScenePropertiesPanel;
	class SceneSerializer;
	class EditorLayer;

	class Scene : public Asset
	{
	public:
		Scene(const std::string& name = "Untitled Scene");
		~Scene();

		void OnUpdate(SceneRenderer& sceneRenderer, const SceneRendererCamera& sceneRendererCamera, Timestep ts);

		void SetViewport(uint32_t width, uint32_t height);

		Entity CreateEntity(const std::string& tagName = "Entity");
		Entity DuplicateEntity(Entity sourceEntity);
		void DestroyEntity(Entity entity);

		void SetName(const std::string& name) { m_Name = name; }
		void SetRendererType(SceneRendererType rendererType) { m_RendererType = rendererType; }
		SceneRendererType GetRendererType() const { return m_RendererType; }

		// Scene components currently cache whether their renderer-side proxy exists.
		// A renderer replacement owns a fresh RenderScene, so every mesh must be
		// registered again before the new renderer can draw it.
		void ResetRendererState();

		inline glm::vec2 GetViewportSize() const { return glm::vec2(m_Width, m_Heigth); }

		static AssetType GetStaticType() { return AssetType::Scene; }
		AssetType GetAssetType() const { return GetStaticType(); }
	private:
		entt::registry m_Registry;
		uint32_t m_Width, m_Heigth;
		std::string m_Name;
		SceneRendererType m_RendererType = SceneRendererType::PathTracer;

		LightEnvironment m_LightEnvironment;
		std::vector<RenderObjectID> m_MeshToRemoved;

		friend class Entity;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;
		friend class ScenePropertiesPanel;
		// Editor-only scene queries (viewport picking) stay outside the renderer.
		friend class EditorLayer;
	};
}


