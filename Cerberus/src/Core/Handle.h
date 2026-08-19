#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

namespace cerberus
{
	class ResourceManager;

	template<typename T, std::size_t TargetPageBytes>
	class Pool;

	template<typename T>
	class Handle
	{
	public:
		using GenerationType = uint32_t;
		using IndexType = uint32_t;

		static constexpr IndexType InvalidIndex = std::numeric_limits<IndexType>::max();

		constexpr Handle() noexcept = default;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Generation != 0 && m_Index != InvalidIndex;
		}

		[[nodiscard]] constexpr GenerationType GetGeneration() const noexcept { return m_Generation; }
		[[nodiscard]] constexpr IndexType GetIndex() const noexcept { return m_Index; }

		// Kept as a source-compatible transition helper for old call sites.
		// Resource generations are now 32-bit and deterministic, not random UUIDs.
		[[nodiscard]] constexpr uint32_t GetUUID_16() const noexcept { return m_Generation; }

		[[nodiscard]] constexpr uint64_t GetValue() const noexcept
		{
			return (static_cast<uint64_t>(m_Generation) << 32u) |
				static_cast<uint64_t>(m_Index);
		}

		[[nodiscard]] constexpr bool operator==(const Handle&) const noexcept = default;

	private:
		constexpr Handle(GenerationType generation, IndexType index) noexcept
			: m_Generation(generation), m_Index(index)
		{
		}

		void Invalidate() noexcept
		{
			m_Generation = 0;
			m_Index = InvalidIndex;
		}

		GenerationType m_Generation = 0;
		IndexType m_Index = InvalidIndex;

		template<typename U, std::size_t PageBytes>
		friend class Pool;
	};
}

namespace std
{
	template <typename T>
	struct hash<cerberus::Handle<T>>
	{
		std::size_t operator()(const cerberus::Handle<T>& handle) const noexcept
		{
			return hash<uint64_t>{}(handle.GetValue());
		}
	};
}
