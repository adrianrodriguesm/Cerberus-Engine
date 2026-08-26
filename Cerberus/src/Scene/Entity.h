#pragma once
#include <entt.hpp>
#include "Scene/Components/Components.h"
#include "Scene/Scene.h"
namespace cerberus
{
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity entity, Scene* scene)
			: m_EntityHandle(entity), m_Scene(scene){}
		Entity(const Entity&) = default;

		template<typename T>
		bool HasComponent()
		{
			return m_Scene->m_Registry.has<T>(m_EntityHandle);
		}

		template<typename T, typename ... Args>
		T& AddComponent(Args&& ... args)
		{
			CORE_ASSERT(!HasComponent<T>(), "Entity already has component!");
			T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
			return component;
		}


		template<typename T>
		void RemovedComponent()
		{
			CORE_ASSERT(HasComponent<T>(), "Entity do not have component");
			return m_Scene->m_Registry.remove<T>(m_EntityHandle);
		}

		template<typename T>
		T& GetComponent()
		{
			CORE_ASSERT(HasComponent<T>(), "Entity do not have component");
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		operator uint32_t () const { return (uint32_t)m_EntityHandle; }
		operator entt::entity() { return m_EntityHandle; }
		operator entt::entity() const { return m_EntityHandle; }
		operator bool() const { return m_EntityHandle != entt::null && m_Scene; }

		bool operator == (const Entity& other) const
		{
			return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
		}
		bool operator != (const Entity& other) const
		{
			return !(*this == other);
		}

	private:
		entt::entity m_EntityHandle = { entt::null };
		Scene* m_Scene = nullptr;
	};
}
