#include "pch.h"
#include "AssetRegistry.h"

namespace cerberus
{
	AssetRegistry::~AssetRegistry()
	{
		m_AssetRegistry.clear();
	}
	AssetMetadata& AssetRegistry::operator[](const std::filesystem::path& path)
	{
		auto key = path;
		CORE_ASSERT(!path.string().empty(), "Invalid path!");
		return m_AssetRegistry[key];
	}

	const AssetMetadata& AssetRegistry::Get(const std::filesystem::path& path) const
	{
		auto key = path;
		CORE_ASSERT(m_AssetRegistry.find(key) != m_AssetRegistry.end(), "Path not found!");
		return m_AssetRegistry.at(key);
	}

	bool AssetRegistry::Contains(const std::filesystem::path& path) const
	{
		auto key = path;
		return m_AssetRegistry.find(key) != m_AssetRegistry.end();
	}

	size_t AssetRegistry::Remove(const std::filesystem::path& path)
	{
		auto key = path;
		return m_AssetRegistry.erase(key);
	}

	void AssetRegistry::Clear()
	{
		m_AssetRegistry.clear();
	}
}