#pragma once
#include "Core/Timestep.h"
#include "Scene/SceneEnvironment.h"
#include <entt.hpp>
#include <glm/glm.hpp>
#include "Asset/Asset.h"
#include "Components/Components.h"
#include "Renderer/RenderObjectID.h"
namespace cerberus
{
	struct SceneRendererCamera;
	class ForwardSceneRenderer;
	class SceneRenderer;
	class Entity;
	class SceneHierarchyPanel;
	class ScenePropertiesPanel;
	class SceneSerializer;
	class EditorLayer;

	class RegisterContext
	{
	public:
		RegisterContext(Scene* scene)
			: m_Scene(scene)
		{}

		void AddMesh(MeshComponent& PrimitiveComponent)
		{
			AddPrimitiveBatches.emplace_back(PrimitiveComponent);
		}

		void Process();

	private:
		Scene* m_Scene;
		std::vector<MeshComponent> AddPrimitiveBatches;
	};

	class Scene : public Asset
	{
	public:
		Scene(const std::string& name = "Untitled Scene");
		~Scene();

		void OnUpdate(Ref<SceneRenderer> sceneRenderer, const SceneRendererCamera& sceneRendererCamera, Timestep ts);

		//void OnViewportResize(uint32_t width, uint32_t height);
		void SetViewport(uint32_t width, uint32_t height);

		Entity CreateEntity(const std::string& tagName = "Entity");
		Entity DuplicateEntity(Entity sourceEntity);
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
		std::vector<Entity> m_DirtyTransform;

		std::vector<RenderObjectID> m_MeshToRemoved;

		friend class Entity;
		friend class ForwardSceneRenderer;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;
		friend class ScenePropertiesPanel;
		// Editor-only scene queries (viewport picking) stay outside the renderer.
		friend class EditorLayer;
	};
}


