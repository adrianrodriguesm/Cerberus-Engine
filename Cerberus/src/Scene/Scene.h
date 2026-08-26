#pragma once
#include "Core/Timestep.h"
#include "Scene/SceneEnvironment.h"
#include <entt.hpp>
#include <glm/glm.hpp>
#include "Asset/Asset.h"
#include "Components/Components.h"
#include "Renderer/Core/RenderObjectID.h"
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

		Entity GetEntityByUUID(UUID uuid);
		// Reparents child under parent (unparenting it from wherever it was
		// first). Refuses the operation if it would create a cycle. Does not
		// touch child's TransformComponent -- callers that already set up the
		// exact local transform they want (a freshly created entity, a
		// duplicate mirroring its source's local transform) should use this
		// directly. A user dragging an EXISTING entity onto another in the
		// Scene Hierarchy should use ReparentEntityPreservingWorldTransform
		// instead, or the entity silently jumps to a new world position (its
		// old local transform gets reinterpreted relative to the new parent).
		void ParentEntity(Entity child, Entity parent);
		void UnparentEntity(Entity child);
		// Same operations, but recompute child's local TransformComponent
		// afterward so its world-space position/rotation/scale is unchanged --
		// what a user dragging an entity in the Scene Hierarchy expects.
		void ReparentEntityPreservingWorldTransform(Entity child, Entity parent);
		void UnparentEntityPreservingWorldTransform(Entity child);
		// World-space transform, composed from RelationshipComponent::Parent up
		// to the root. Falls back to the entity's own local transform when it
		// has no parent.
		glm::mat4 GetWorldSpaceTransform(Entity entity);
		bool IsWorldTransformDirty(Entity entity);

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
		Entity CreateEntityWithUUID(UUID uuid, const std::string& tagName);

		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		uint32_t m_Width, m_Heigth;
		std::string m_Name;
		// Defaulted to Forward for now while it's under active development and
		// gets loaded on every test launch; flip back to PathTracer once Forward
		// is done, or once scenes are re-saved with an explicit Renderer field.
		SceneRendererType m_RendererType = SceneRendererType::Forward;

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


