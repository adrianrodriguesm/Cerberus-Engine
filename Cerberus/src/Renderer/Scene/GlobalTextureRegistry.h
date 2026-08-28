#pragma once

#include "Asset/Asset.h"
#include "Core/Handle.h"

#include <span>
#include <unordered_map>
#include <vector>

namespace cerberus
{
	class Texture2D;

	// Scene-global bindless texture registry shared by raster and ray tracing.
	// Indices remain stable until ReleaseTexture/Shutdown and slot zero is always
	// the renderer-owned fallback texture.
	class GlobalTextureRegistry final
	{
	public:
		void Initialize(Handle<Texture2D> fallbackTexture);
		void Shutdown();

		uint32_t RegisterTexture(AssetHandle textureAsset, bool srgb);
		void ReleaseTexture(AssetHandle textureAsset, bool srgb);

		uint32_t GetCapacity() const { return m_Capacity; }
		uint64_t GetRevision() const { return m_Revision; }
		std::span<const Handle<Texture2D>> GetTextureHandles() const { return m_TextureSlots; }

	private:
		struct TextureKey
		{
			AssetHandle Asset = 0;
			bool SRGB = false;

			bool operator==(const TextureKey& other) const
			{
				return Asset == other.Asset && SRGB == other.SRGB;
			}
		};

		struct TextureKeyHash
		{
			size_t operator()(const TextureKey& key) const
			{
				return std::hash<uint64_t>{}(static_cast<uint64_t>(key.Asset)) ^
					(std::hash<bool>{}(key.SRGB) << 1);
			}
		};

		struct Entry
		{
			uint32_t Slot = 0;
			uint32_t ReferenceCount = 0;
		};

		Handle<Texture2D> m_FallbackTexture;
		uint32_t m_Capacity = 0;
		uint64_t m_Revision = 0;
		std::vector<Handle<Texture2D>> m_TextureSlots;
		std::vector<uint32_t> m_FreeSlots;
		std::unordered_map<TextureKey, Entry, TextureKeyHash> m_Entries;
	};
}
