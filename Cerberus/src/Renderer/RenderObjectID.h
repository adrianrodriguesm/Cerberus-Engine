#pragma once

#include <cstdint>
#include <functional>

#include "Core/UUID.h"

namespace cerberus
{
	// Strong renderer-side identity for one logical object in a Scene. This must
	// not be confused with AssetHandle/MeshHandle: several RenderObjectIDs may
	// reference the same mesh asset while keeping independent transforms and
	// material overrides.
	class RenderObjectID final
	{
	public:
		RenderObjectID() = default;
		explicit RenderObjectID(const UUID& entityID)
			: m_Value(static_cast<uint64_t>(entityID))
		{
		}

		bool IsValid() const { return m_Value != 0; }
		explicit operator bool() const { return IsValid(); }
		uint64_t GetValue() const { return m_Value; }

		bool operator==(const RenderObjectID& other) const = default;

	private:
		uint64_t m_Value = 0;
	};
}

namespace std
{
	template <>
	struct hash<cerberus::RenderObjectID>
	{
		std::size_t operator()(const cerberus::RenderObjectID& id) const noexcept
		{
			return std::hash<uint64_t>()(id.GetValue());
		}
	};
}
