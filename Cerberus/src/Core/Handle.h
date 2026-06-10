#pragma once
#include "Core/UUID.h"
namespace cerberus
{
	class ResourceManager;

	template<typename T>
	class Pool;

	template<typename T>
	class Handle
	{
	public:
		Handle() : m_ID(0), m_Index(0){}
		bool IsValid() const { return m_ID != 0; }
		const UUID_16 GetUUID_16() const { return m_ID; }

		bool operator == (const Handle<T>& otherHandle) const 
		{
			return otherHandle.m_ID == m_ID && otherHandle.m_Index == m_Index;;
		}

	private:
		Handle(uint16_t generationID, uint16_t index) : m_ID(generationID), m_Index(index) {}

		void Invalidate() 
		{
			m_ID = 0;
			m_Index = 0;
		}

		// This 16+16 type is good enought for GPU side assets (resources)
		UUID_16 m_ID = 0;
		uint16_t m_Index = 0;

		template<typename U> friend class Pool;
	};
}

// Creation of a hash function for the UUID
// In cerberus this is used to allow the use of the UUID as a key for an unordered_map (hashmap)
namespace std
{

	template <typename T>
	struct hash<cerberus::Handle<T>>
	{
		std::size_t operator()(const cerberus::Handle<T>& uuid) const
		{
			return hash<uint16_t>()((uint16_t)uuid.GetUUID_16());
		}
	};
}