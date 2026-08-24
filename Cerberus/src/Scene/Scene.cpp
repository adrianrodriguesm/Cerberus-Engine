#include "pch.h"
#include "Scene.h"
#include "Renderer/SceneRenderer/SceneRenderer.h"
#include "Scene/Entity.h"
#include "Renderer/Core/RendererResources.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Asset/AssetManager.h"
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
				const auto& [transform, mesh] = group.get<TransformComponent, MeshComponent>(entity);
				const RenderObjectID renderObjectID{ m_Registry.get<IDComponent>(entity).ID };
				if (!mesh.m_Registered)
				{
					bool registered = sceneRenderer.RegisterMesh(
						renderObjectID,
						AssetManager::GetAsset<Mesh>(mesh.MeshHandle),
						mesh.MaterialTable,
						transform.GetTransform());
					mesh.m_Registered = registered;
					transform.m_TransformStateDirty = false;
					}
				else if (transform.IsTransformDirty())
				{
					sceneRenderer.UpdateMeshTransform(renderObjectID, transform.GetTransform());
					transform.m_TransformStateDirty = false;
				}

					
			}
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
		Entity entity = { m_Registry.create(), this };

		auto& idComponent = entity.AddComponent<IDComponent>();
		idComponent.ID = UUID();

		entity.AddComponent<TagComponent>(tagName);
		entity.AddComponent<TransformComponent>();

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

		// IDComponent and TagComponent are intentionally not copied. CreateEntity
		// already generated a fresh UUID/RenderObjectID and the duplicate tag above.
		if (sourceEntity.HasComponent<MeshComponent>())
			duplicateEntity.AddComponent<MeshComponent>(sourceEntity.GetComponent<MeshComponent>());

		if (sourceEntity.HasComponent<DirectionalLightComponent>())
			duplicateEntity.AddComponent<DirectionalLightComponent>(sourceEntity.GetComponent<DirectionalLightComponent>());

		return duplicateEntity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		if (entity.HasComponent<MeshComponent>())
		{
			const RenderObjectID renderObjectID{ entity.GetComponent<IDComponent>().ID };
			m_MeshToRemoved.push_back(renderObjectID);
		}
		m_Registry.destroy(entity);
	}

	void Scene::SetViewport(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Heigth = height;
	}
}
