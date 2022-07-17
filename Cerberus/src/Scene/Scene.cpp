#include "pch.h"
#include "Scene.h"
#include "Renderer/SceneRenderer/SceneRenderer.h"
#include "Scene/Entity.h"
#include "Renderer/Renderer.h"
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

	void Scene::OnUpdate(Ref<SceneRenderer> sceneRenderer, const SceneRendererCamera& sceneRendererCamera, Timestep ts)
	{
		m_LightEnvironment = {};
		// Enviornment
		{
			// Sky Light - Environment Map
			{
				auto& view = m_Registry.view<SkyLightComponent>();
				CORE_ASSERT(view.size() <= 1, "There is more than one SkyLightComponenet in the Scene!");
				if (view.size())
				{
					Entity cerberusEnt{ view[0], this };
					auto& skyLight = cerberusEnt.GetComponent<SkyLightComponent>();
					Ref<EnvironmentMap> environment = AssetManager::GetAsset<EnvironmentMap>(skyLight.EnvironmentHandle);
					m_LightEnvironment.SkyLight.EnvironmentMap = environment ? environment : Renderer::GetEmptyEnvironmentMap();
					m_LightEnvironment.SkyLight.Multiplier = skyLight.Intensity;
					m_LightEnvironment.SkyLight.SampleLOD = skyLight.SampleLOD;
				}
				else
				{
					m_LightEnvironment.SkyLight.EnvironmentMap = Renderer::GetEmptyEnvironmentMap();
					m_LightEnvironment.SkyLight.Multiplier = 1.f;
					m_LightEnvironment.SkyLight.SampleLOD = 1;
				}
			}
			// Directional Light
			{
				auto& view = m_Registry.view<DirectionalLightComponent>();
				uint32_t directionalLightIndex = 0;
				for (auto& entity : view)
				{
					if (directionalLightIndex > m_LightEnvironment.DirectionalLights.size())
						break;

					Entity cerberusEnt{ entity, this };
					auto& directionLight = cerberusEnt.GetComponent<DirectionalLightComponent>();
					auto& transform = cerberusEnt.GetComponent<TransformComponent>();
					glm::vec3 direction = -glm::normalize(glm::mat3(transform.GetTransform()) * -glm::vec3(1.f));
					m_LightEnvironment.DirectionalLights[directionalLightIndex++] =
					{
						direction,
						directionLight.Radiance,
						directionLight.Intensity
					};
				}
			}
		}
		sceneRenderer->BeginScene(sceneRendererCamera);
		// Submit Meshes
		{
			auto& group = m_Registry.group<TransformComponent>(entt::get<MeshComponent>);
			for (auto& entity : group)
			{
				auto& [transform, mesh] = group.get<TransformComponent, MeshComponent>(entity);
				if(mesh.MeshHandle != 0)
					sceneRenderer->SubmitMesh(AssetManager::GetAsset<Mesh>(mesh.MeshHandle), mesh.MaterialTable, transform.GetTransform());
			}
		}
		sceneRenderer->EndScene();
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

	void Scene::DestroyEntity(Entity entity)
	{
		m_Registry.destroy(entity);
	}

	void Scene::SetViewport(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Heigth = height;
	}
}