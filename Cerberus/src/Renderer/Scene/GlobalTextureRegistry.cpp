#include "pch.h"
#include "GlobalTextureRegistry.h"

#include "Asset/AssetManager.h"
#include "Asset/Assets/TextureAsset.h"
#include "Renderer/Core/RendererContext.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Scene/Proxies/MaterialProxy.h"

#include <algorithm>

namespace cerberus
{
	void GlobalTextureRegistry::Initialize(Handle<Texture2D> fallbackTexture)
	{
		CORE_ASSERT(!m_FallbackTexture.IsValid(), "GlobalTextureRegistry is already initialized");
		CORE_ASSERT(fallbackTexture.IsValid(), "GlobalTextureRegistry requires a fallback texture");

		m_FallbackTexture = fallbackTexture;
		m_Capacity = RendererContext::GetCurrentDevice()->GetBindlessTextureCapacity();
		m_TextureSlots = { fallbackTexture }; // Slot zero is permanently valid.
		m_Revision = 1;

		NV_CORE_INFO("Global bindless texture registry capacity: {0}", m_Capacity);
	}

	void GlobalTextureRegistry::Shutdown()
	{
		for (uint32_t slot = 1; slot < m_TextureSlots.size(); ++slot)
		{
			const Handle<Texture2D> texture = m_TextureSlots[slot];
			if (texture.IsValid() && texture != m_FallbackTexture)
				ResourceManager::DestroyTexture2D(texture);
		}

		m_Entries.clear();
		m_FreeSlots.clear();
		m_TextureSlots.clear();
		m_FallbackTexture = {};
		m_Capacity = 0;
		++m_Revision;
	}

	uint32_t GlobalTextureRegistry::RegisterTexture(AssetHandle textureAssetHandle, bool srgb)
	{
		if (textureAssetHandle == 0)
			return InvalidMaterialTextureIndex;

		const TextureKey key{ textureAssetHandle, srgb };
		if (auto existing = m_Entries.find(key); existing != m_Entries.end())
		{
			++existing->second.ReferenceCount;
			return existing->second.Slot;
		}

		Ref<TextureAsset> textureAsset = AssetManager::GetAsset<TextureAsset>(textureAssetHandle);
		if (!textureAsset || !textureAsset->Loaded())
		{
			NV_CORE_WARN("Material texture asset {0} could not be loaded", static_cast<uint64_t>(textureAssetHandle));
			return InvalidMaterialTextureIndex;
		}

		CORE_ASSERT(m_TextureSlots.size() < m_Capacity || !m_FreeSlots.empty(),
			"Global bindless texture registry capacity exceeded");

		Handle<Texture2D> gpuTexture = ResourceManager::CreateTexture2D(
			textureAsset,
			srgb ? TextureColorSpace::SRGB : TextureColorSpace::Linear);
		CORE_ASSERT(gpuTexture.IsValid(), "Failed to create a bindless material texture");

		uint32_t slot = 0;
		if (!m_FreeSlots.empty())
		{
			slot = m_FreeSlots.back();
			m_FreeSlots.pop_back();
			m_TextureSlots[slot] = gpuTexture;
		}
		else
		{
			slot = static_cast<uint32_t>(m_TextureSlots.size());
			m_TextureSlots.push_back(gpuTexture);
		}

		m_Entries.emplace(key, Entry{ slot, 1 });
		++m_Revision;
		return slot;
	}

	void GlobalTextureRegistry::ReleaseTexture(AssetHandle textureAssetHandle, bool srgb)
	{
		const TextureKey key{ textureAssetHandle, srgb };
		auto entryIt = m_Entries.find(key);
		if (entryIt == m_Entries.end())
			return;

		Entry& entry = entryIt->second;
		if (--entry.ReferenceCount > 0)
			return;

		CORE_ASSERT(entry.Slot > 0 && entry.Slot < m_TextureSlots.size(), "Invalid bindless texture slot");
		const Handle<Texture2D> texture = m_TextureSlots[entry.Slot];
		if (texture.IsValid() && texture != m_FallbackTexture)
			ResourceManager::DestroyTexture2D(texture);

		m_TextureSlots[entry.Slot] = m_FallbackTexture;
		m_FreeSlots.push_back(entry.Slot);
		m_Entries.erase(entryIt);
		++m_Revision;
	}
}
