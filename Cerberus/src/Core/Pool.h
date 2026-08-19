#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "Core/Handle.h"

namespace cerberus
{
	// Stable-address, generational object pool.
	//
	// Objects live inline inside independently allocated pages. Growing the page
	// pointer array therefore never moves an existing T. Slots are recycled through
	// an intrusive free list and stale handles are rejected by their generation.
	template<typename T, std::size_t TargetPageBytes = 16u * 1024u>
	class Pool
	{
		static_assert(std::is_object_v<T> && !std::is_array_v<T>,
			"Pool<T> requires a non-array object type");

	public:
		using HandleType = Handle<T>;
		using GenerationType = typename HandleType::GenerationType;
		using IndexType = typename HandleType::IndexType;

	private:
		static constexpr IndexType InvalidIndex = HandleType::InvalidIndex;

		struct Slot
		{
			alignas(T) std::byte Storage[sizeof(T)];
			GenerationType Generation = 1;
			IndexType NextFree = InvalidIndex;
			bool Alive = false;

			T* StoragePointer() noexcept
			{
				return reinterpret_cast<T*>(Storage);
			}

			const T* StoragePointer() const noexcept
			{
				return reinterpret_cast<const T*>(Storage);
			}

			T* Object() noexcept
			{
				return std::launder(StoragePointer());
			}

			const T* Object() const noexcept
			{
				return std::launder(StoragePointer());
			}
		};

		static constexpr std::size_t SlotsPerPage =
			TargetPageBytes > sizeof(Slot) ? TargetPageBytes / sizeof(Slot) : 1u;

		struct Page
		{
			std::array<Slot, SlotsPerPage> Slots;
		};

	public:
		Pool() = default;

		~Pool()
		{
			if (m_LiveCount > 0)
				DestroyLiveObjects();
		}

		Pool(const Pool&) = delete;
		Pool& operator=(const Pool&) = delete;
		Pool(Pool&&) = delete;
		Pool& operator=(Pool&&) = delete;

		void Clear() noexcept
		{
			// End the lifetime of every live object exactly once and invalidate all
			// handles that referred to one of them.
			if (m_LiveCount > 0)
				DestroyLiveObjects();

			// Retain the pages, but rebuild a canonical free list containing every
			// slot. The next allocation starts at index zero.
			m_FreeHead = InvalidIndex;
			for (std::size_t pageIndex = m_Pages.size(); pageIndex > 0; --pageIndex)
			{
				Page& page = *m_Pages[pageIndex - 1];
				const uint64_t pageBase = static_cast<uint64_t>(pageIndex - 1) * SlotsPerPage;

				for (std::size_t slotIndex = SlotsPerPage; slotIndex > 0; --slotIndex)
				{
					Slot& slot = page.Slots[slotIndex - 1];
					slot.NextFree = m_FreeHead;
					m_FreeHead = static_cast<IndexType>(pageBase + slotIndex - 1);
				}
			}
		}

	private:
		void DestroyLiveObjects() noexcept
		{
			for (const std::unique_ptr<Page>& page : m_Pages)
			{
				for (Slot& slot : page->Slots)
				{
					if (!slot.Alive)
						continue;

					std::destroy_at(slot.Object());
					slot.Alive = false;
					slot.Generation = NextGeneration(slot.Generation);
				}
			}
			m_LiveCount = 0;
		}

	public:

		template<typename... Args>
		HandleType Emplace(Args&&... args)
		{
			if (m_FreeHead == InvalidIndex)
				AddPage();

			const IndexType index = m_FreeHead;
			Slot& slot = GetSlot(index);
			const IndexType nextFree = slot.NextFree;

			// Update the free list only after successful construction. If T's
			// constructor throws, this slot remains available and pool state is intact.
			std::construct_at(slot.StoragePointer(), std::forward<Args>(args)...);

			slot.Alive = true;
			slot.NextFree = InvalidIndex;
			m_FreeHead = nextFree;
			++m_LiveCount;

			return HandleType(slot.Generation, index);
		}

		[[nodiscard]] T* Get(const HandleType handle) noexcept
		{
			if (!IsHandleLive(handle))
				return nullptr;

			return GetSlot(handle.m_Index).Object();
		}

		[[nodiscard]] const T* Get(const HandleType handle) const noexcept
		{
			if (!IsHandleLive(handle))
				return nullptr;

			return GetSlot(handle.m_Index).Object();
		}

		void Remove(const HandleType handle) noexcept
		{
			if (!IsHandleLive(handle))
				return;

			Slot& slot = GetSlot(handle.m_Index);
			std::destroy_at(slot.Object());

			slot.Alive = false;
			slot.Generation = NextGeneration(slot.Generation);
			slot.NextFree = m_FreeHead;
			m_FreeHead = handle.m_Index;
			--m_LiveCount;
		}

		[[nodiscard]] bool Contains(const HandleType handle) const noexcept
		{
			return IsHandleLive(handle);
		}

		[[nodiscard]] std::size_t Size() const noexcept { return m_LiveCount; }
		[[nodiscard]] std::size_t Capacity() const noexcept { return m_Pages.size() * SlotsPerPage; }
		[[nodiscard]] static constexpr std::size_t PageCapacity() noexcept { return SlotsPerPage; }

	private:
		static constexpr GenerationType NextGeneration(GenerationType generation) noexcept
		{
			++generation;
			return generation == 0 ? 1 : generation;
		}

		[[nodiscard]] bool IsHandleLive(const HandleType handle) const noexcept
		{
			if (!handle.IsValid() || static_cast<std::size_t>(handle.m_Index) >= Capacity())
				return false;

			const Slot& slot = GetSlot(handle.m_Index);
			return slot.Alive && slot.Generation == handle.m_Generation;
		}

		Slot& GetSlot(IndexType index) noexcept
		{
			return m_Pages[index / SlotsPerPage]->Slots[index % SlotsPerPage];
		}

		const Slot& GetSlot(IndexType index) const noexcept
		{
			return m_Pages[index / SlotsPerPage]->Slots[index % SlotsPerPage];
		}

		void AddPage()
		{
			constexpr uint64_t MaximumSlotCount = static_cast<uint64_t>(InvalidIndex);
			const uint64_t firstIndex = static_cast<uint64_t>(m_Pages.size()) * SlotsPerPage;

			if (firstIndex + SlotsPerPage > MaximumSlotCount)
				throw std::length_error("Pool<T> exhausted its 32-bit handle index space");

			// std::make_unique<Page> uses aligned new when Page/T requires extended
			// alignment. Moving this unique_ptr later never moves the Page itself.
			m_Pages.emplace_back(std::make_unique<Page>());
			Page& page = *m_Pages.back();

			IndexType newFreeHead = m_FreeHead;
			for (std::size_t slotIndex = SlotsPerPage; slotIndex > 0; --slotIndex)
			{
				Slot& slot = page.Slots[slotIndex - 1];
				slot.NextFree = newFreeHead;
				newFreeHead = static_cast<IndexType>(firstIndex + slotIndex - 1);
			}

			m_FreeHead = newFreeHead;
		}

	private:
		std::vector<std::unique_ptr<Page>> m_Pages;
		IndexType m_FreeHead = InvalidIndex;
		std::size_t m_LiveCount = 0;
	};
}
