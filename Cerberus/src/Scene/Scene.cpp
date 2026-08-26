#include "pch.h"
#include "Scene.h"
#include "Core/Profiler.h"
#include "Renderer/SceneRenderer/SceneRenderer.h"
#include "Scene/Entity.h"
#include "Renderer/Core/RendererResources.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Asset/AssetManager.h"
#include <algorithm>
namespace cerberus
{
	Scene::Scene(const std::string& name)
		: m_Name(name)
	{
	}
	Scene::~Scene()
	{
	}

	void Scene::OnUpdate(SceneRenderer& sceneRenderer, const SceneRendererCamera& sceneRendererCamera, Timestep ts)
	{
		CORE_PROFILE_FUNCTION();

		if (m_MeshToRemoved.size() > 0)
		{
			for (RenderObjectID renderObjectID : m_MeshToRemoved)
				sceneRenderer.UnregisterMesh(renderObjectID);
			m_MeshToRemoved.clear();
		}
		m_LightEnvironment = {};
		uint32_t directionalLightCount = 0;
		// Environment
		{
			// Sky Light - Environment Map
			{
				const auto& view = m_Registry.view<SkyLightComponent>();
				CORE_ASSERT(view.size() <= 1, "There is more than one SkyLightComponenet in the Scene!");
				if (view.size())
				{
					Entity cerberusEnt{ view[0], this };
					auto& skyLight = cerberusEnt.GetComponent<SkyLightComponent>();
					Ref<EnvironmentMap> environment = AssetManager::GetAsset<EnvironmentMap>(skyLight.EnvironmentHandle);
					m_LightEnvironment.SkyLight.EnvironmentMap = environment ? environment : RendererResources::GetEmptyEnvironmentMap();
					m_LightEnvironment.SkyLight.Multiplier = skyLight.Intensity;
					m_LightEnvironment.SkyLight.SampleLOD = skyLight.SampleLOD;
				}
				else
				{
					m_LightEnvironment.SkyLight.EnvironmentMap = RendererResources::GetEmptyEnvironmentMap();
					m_LightEnvironment.SkyLight.Multiplier = 1.f;
					m_LightEnvironment.SkyLight.SampleLOD = 1;
				}
			}
			// Directional Light
			{
				const auto& view = m_Registry.view<DirectionalLightComponent>();
				for (auto& entity : view)
				{
					if (directionalLightCount >= m_LightEnvironment.DirectionalLights.size())
						break;

					Entity cerberusEnt{ entity, this };
					auto& directionLight = cerberusEnt.GetComponent<DirectionalLightComponent>();
					auto& transform = cerberusEnt.GetComponent<TransformComponent>();
					glm::vec3 direction = -glm::normalize(glm::mat3(transform.GetTransform()) * -glm::vec3(1.f));
					m_LightEnvironment.DirectionalLights[directionalLightCount++] =
					{
						direction,
						directionLight.Radiance,
						directionLight.Intensity
					};
				}
			}
		}
		sceneRenderer.SetSkyLight(
			m_LightEnvironment.SkyLight.EnvironmentMap,
			m_LightEnvironment.SkyLight.Multiplier,
			m_LightEnvironment.SkyLight.SampleLOD);
		sceneRenderer.SetDirectionalLights(std::span<const DirectionalLight>(
			m_LightEnvironment.DirectionalLights.data(),
			directionalLightCount));
		// Submit Meshes
		{
			const auto& group = m_Registry.group<TransformComponent>(entt::get<MeshComponent>);
			for (auto& entity : group)
			{
				auto& mesh = group.get<MeshComponent>(entity);
				Entity meshEntity{ entity, this };
				const RenderObjectID renderObjectID{ m_Registry.get<IDComponent>(entity).ID };
				if (!mesh.m_Registered)
				{
					bool registered = sceneRenderer.RegisterMesh(
						renderObjectID,
						AssetManager::GetAsset<Mesh>(mesh.MeshHandle),
						mesh.MaterialTable,
						GetWorldSpaceTransform(meshEntity),
						mesh.SubmeshIndex);
					mesh.m_Registered = registered;
				}
				else if (IsWorldTransformDirty(meshEntity))
				{
					sceneRenderer.UpdateMeshTransform(renderObjectID, GetWorldSpaceTransform(meshEntity));
				}
			}
		}
		// Clear every transform's per-frame dirty flag now that this frame's
		// world-transform reads above have observed it. Done as a separate pass
		// over every TransformComponent (not just mesh entities) so a non-mesh
		// "empty group" parent's dirty flag also clears -- otherwise it would
		// never be seen again after the first move and its mesh children would
		// re-send GPU transform updates every frame.
		{
			auto view = m_Registry.view<TransformComponent>();
			for (auto entity : view)
				view.get<TransformComponent>(entity).m_TransformStateDirty = false;
		}
		// EditorLayer owns the only SceneRenderer and processes renderer changes at
		// the post-present lifecycle boundary, after this queue has executed. Capturing
		// a non-owning pointer avoids shared ownership while preserving that ordering.
		SceneRenderer* renderer = &sceneRenderer;
		RenderQueueManager::Submit([renderer, sceneRendererCamera]() mutable
		{
			renderer->Render(sceneRendererCamera);
		});
	}

	void Scene::ResetRendererState()
	{
		m_MeshToRemoved.clear();
		auto meshView = m_Registry.view<MeshComponent>();
		for (const entt::entity entity : meshView)
			meshView.get<MeshComponent>(entity).m_Registered = false;
	}

	Entity Scene::CreateEntity(const std::string& tagName)
	{
		return CreateEntityWithUUID(UUID(), tagName);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& tagName)
	{
		Entity entity = { m_Registry.create(), this };

		auto& idComponent = entity.AddComponent<IDComponent>();
		idComponent.ID = uuid;

		entity.AddComponent<TagComponent>(tagName);
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<RelationshipComponent>();

		m_EntityMap[uuid] = entity;

		return entity;
	}

	Entity Scene::DuplicateEntity(Entity sourceEntity)
	{
		if (!sourceEntity)
			return {};

		// A Scene currently supports a single SkyLight. Refuse the operation
		// instead of creating a duplicate that would fail during Scene::OnUpdate.
		if (sourceEntity.HasComponent<SkyLightComponent>())
		{
			NV_CORE_WARN("SkyLight is a singleton Scene component and cannot be duplicated");
			return {};
		}

		const std::string sourceTag = sourceEntity.HasComponent<TagComponent>()
			? sourceEntity.GetComponent<TagComponent>().Tag
			: "Entity";
		Entity duplicateEntity = CreateEntity(sourceTag + " Copy");

		if (sourceEntity.HasComponent<TransformComponent>())
		{
			auto& duplicateTransform = duplicateEntity.GetComponent<TransformComponent>();
			duplicateTransform = sourceEntity.GetComponent<TransformComponent>();
			duplicateTransform.MarkTransformAsDirty();
		}

		// IDComponent, TagComponent and RelationshipComponent are intentionally
		// not copied. CreateEntity already generated a fresh UUID/RenderObjectID,
		// the duplicate tag above, and an empty RelationshipComponent -- children
		// are re-parented onto it below instead.
		if (sourceEntity.HasComponent<MeshComponent>())
			duplicateEntity.AddComponent<MeshComponent>(sourceEntity.GetComponent<MeshComponent>());

		if (sourceEntity.HasComponent<DirectionalLightComponent>())
			duplicateEntity.AddComponent<DirectionalLightComponent>(sourceEntity.GetComponent<DirectionalLightComponent>());

		// Recursively duplicate the whole subtree so duplicating a parent
		// duplicates its children too, re-parented onto the new entity.
		for (UUID childID : sourceEntity.GetComponent<RelationshipComponent>().Children)
		{
			Entity sourceChild = GetEntityByUUID(childID);
			if (!sourceChild)
				continue;
			Entity duplicateChild = DuplicateEntity(sourceChild);
			if (duplicateChild)
				ParentEntity(duplicateChild, duplicateEntity);
		}

		return duplicateEntity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		// Cascade to children first. Copy the list -- UnparentEntity (called
		// recursively via DestroyEntity) mutates a parent's Children vector as
		// each child is removed from it.
		std::vector<UUID> children = entity.GetComponent<RelationshipComponent>().Children;
		for (UUID childID : children)
		{
			Entity child = GetEntityByUUID(childID);
			if (child)
				DestroyEntity(child);
		}

		UnparentEntity(entity);

		if (entity.HasComponent<MeshComponent>())
		{
			const RenderObjectID renderObjectID{ entity.GetComponent<IDComponent>().ID };
			m_MeshToRemoved.push_back(renderObjectID);
		}

		m_EntityMap.erase(entity.GetComponent<IDComponent>().ID);
		m_Registry.destroy(entity);
	}

	Entity Scene::GetEntityByUUID(UUID uuid)
	{
		auto it = m_EntityMap.find(uuid);
		if (it == m_EntityMap.end())
			return {};
		return { it->second, this };
	}

	void Scene::ParentEntity(Entity child, Entity parent)
	{
		if (!child || !parent || child == parent)
			return;

		const UUID childID = child.GetComponent<IDComponent>().ID;

		// Refuse a reparent that would create a cycle (parent is already a
		// descendant of child).
		for (Entity ancestor = parent; ancestor; )
		{
			UUID ancestorParentID = ancestor.GetComponent<RelationshipComponent>().Parent;
			if (ancestorParentID == childID)
				return;
			ancestor = ancestorParentID ? GetEntityByUUID(ancestorParentID) : Entity{};
		}

		UnparentEntity(child);

		child.GetComponent<RelationshipComponent>().Parent = parent.GetComponent<IDComponent>().ID;
		parent.GetComponent<RelationshipComponent>().Children.push_back(childID);
	}

	void Scene::UnparentEntity(Entity child)
	{
		auto& childRelationship = child.GetComponent<RelationshipComponent>();
		if (!childRelationship.Parent)
			return;

		Entity parent = GetEntityByUUID(childRelationship.Parent);
		if (parent)
		{
			auto& siblings = parent.GetComponent<RelationshipComponent>().Children;
			const UUID childID = child.GetComponent<IDComponent>().ID;
			siblings.erase(std::remove(siblings.begin(), siblings.end(), childID), siblings.end());
		}
		childRelationship.Parent = 0;
	}

	void Scene::ReparentEntityPreservingWorldTransform(Entity child, Entity parent)
	{
		if (!child || !parent || child == parent)
			return;

		const glm::mat4 childWorldTransform = GetWorldSpaceTransform(child);
		const UUID childID = child.GetComponent<IDComponent>().ID;

		ParentEntity(child, parent);
		// ParentEntity silently no-ops on a cycle; only fix up the local
		// transform if the reparent actually happened.
		if (child.GetComponent<RelationshipComponent>().Parent != parent.GetComponent<IDComponent>().ID)
			return;

		const glm::mat4 newParentWorldTransform = GetWorldSpaceTransform(parent);
		const glm::mat4 newLocalTransform = glm::inverse(newParentWorldTransform) * childWorldTransform;

		glm::vec3 translation, rotation, scale;
		math::DecomposeTransform(newLocalTransform, translation, rotation, scale);
		auto& tc = child.GetComponent<TransformComponent>();
		tc.SetTranslation(translation);
		tc.SetRotation(glm::degrees(rotation));
		tc.SetScale(scale);
	}

	void Scene::UnparentEntityPreservingWorldTransform(Entity child)
	{
		if (!child || !child.GetComponent<RelationshipComponent>().Parent)
			return;

		const glm::mat4 childWorldTransform = GetWorldSpaceTransform(child);
		UnparentEntity(child);

		// No parent left, so this world transform is now exactly the local one.
		glm::vec3 translation, rotation, scale;
		math::DecomposeTransform(childWorldTransform, translation, rotation, scale);
		auto& tc = child.GetComponent<TransformComponent>();
		tc.SetTranslation(translation);
		tc.SetRotation(glm::degrees(rotation));
		tc.SetScale(scale);
	}

	glm::mat4 Scene::GetWorldSpaceTransform(Entity entity)
	{
		glm::mat4 transform = entity.GetComponent<TransformComponent>().GetTransform();
		UUID parentID = entity.GetComponent<RelationshipComponent>().Parent;
		if (!parentID)
			return transform;

		Entity parent = GetEntityByUUID(parentID);
		return parent ? GetWorldSpaceTransform(parent) * transform : transform;
	}

	bool Scene::IsWorldTransformDirty(Entity entity)
	{
		if (entity.GetComponent<TransformComponent>().IsTransformDirty())
			return true;

		UUID parentID = entity.GetComponent<RelationshipComponent>().Parent;
		if (!parentID)
			return false;

		Entity parent = GetEntityByUUID(parentID);
		return parent && IsWorldTransformDirty(parent);
	}

	void Scene::SetViewport(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Heigth = height;
	}
}
