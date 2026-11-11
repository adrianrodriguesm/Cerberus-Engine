#include "pch.h"
#include "GeometryPool.h"

#include "Renderer/Core/Renderer.h"
#include "Renderer/Memory/DeferredResourceDeletionQueue.h"
#include "Renderer/RHI/Command/TransferCommandList.h"
#include "Renderer/RHI/Command/TransientCommandBuffer.h"
#include "Renderer/RHI/ResourceManager.h"

#include <algorithm>
#include <cstring>

namespace cerberus
{
	void GeometryPool::FreeList::Init(uint32_t capacity)
	{
		Capacity = capacity;
		Regions = { { 0u, capacity } };
		UsedElements = 0;
	}

	bool GeometryPool::FreeList::Allocate(uint32_t count, uint32_t& outOffset)
	{
		if (count == 0)
		{
			outOffset = 0;
			return true;
		}

		for (size_t i = 0; i < Regions.size(); ++i)
		{
			if (Regions[i].Count < count)
				continue;

			outOffset = Regions[i].Offset;
			if (Regions[i].Count == count)
				Regions.erase(Regions.begin() + i);
			else
			{
				Regions[i].Offset += count;
				Regions[i].Count -= count;
			}

			UsedElements += count;
			return true;
		}

		return false;
	}

	void GeometryPool::FreeList::Free(uint32_t offset, uint32_t count)
	{
		if (count == 0)
			return;

		CORE_ASSERT(UsedElements >= count, "GeometryPool free list underflow");
		UsedElements -= count;

		// Insertion point keeps Regions ordered by offset, which is what makes
		// the adjacency checks below (coalescing with the immediate
		// neighbours) sufficient instead of needing a full merge pass.
		const auto insertAt = std::lower_bound(Regions.begin(), Regions.end(), offset,
			[](const FreeRegion& region, uint32_t value) { return region.Offset < value; });

		auto inserted = Regions.insert(insertAt, FreeRegion{ offset, count });

		if (inserted != Regions.end() - 1)
		{
			auto next = inserted + 1;
			if (inserted->Offset + inserted->Count == next->Offset)
			{
				inserted->Count += next->Count;
				Regions.erase(next);
			}
		}
		if (inserted != Regions.begin())
		{
			auto prev = inserted - 1;
			if (prev->Offset + prev->Count == inserted->Offset)
			{
				prev->Count += inserted->Count;
				Regions.erase(inserted);
			}
		}
	}

	GeometryPool::GeometryPool(uint32_t vertexCapacityBytes, uint32_t indexCapacityBytes)
	{
		const uint32_t vertexCapacityCount = vertexCapacityBytes / VertexStrideBytes;
		const uint32_t indexCapacityCount = indexCapacityBytes / sizeof(uint32_t);
		m_VertexFreeList.Init(vertexCapacityCount);
		m_IndexFreeList.Init(indexCapacityCount);

		GPUBufferSpecification vertexSpec;
		vertexSpec.DebugName = "GeometryPool Vertices";
		vertexSpec.MemoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		vertexSpec.Size = vertexCapacityCount * VertexStrideBytes;
		vertexSpec.Usage = GPUBufferUsage::VertexBufferUsage;
		m_VertexBuffer = ResourceManager::CreateBuffer(vertexSpec);

		GPUBufferSpecification indexSpec;
		indexSpec.DebugName = "GeometryPool Indices";
		indexSpec.MemoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		indexSpec.Size = indexCapacityCount * static_cast<uint32_t>(sizeof(uint32_t));
		indexSpec.Usage = GPUBufferUsage::IndexBufferUsage;
		m_IndexBuffer = ResourceManager::CreateBuffer(indexSpec);
	}

	GeometryPool::~GeometryPool()
	{
		ResourceManager::DestroyBuffer(m_VertexBuffer);
		ResourceManager::DestroyBuffer(m_IndexBuffer);
	}

	GeometryAllocation GeometryPool::Allocate(uint32_t vertexCount, uint32_t indexCount)
	{
		GeometryAllocation allocation;
		allocation.VertexCount = vertexCount;
		allocation.IndexCount = indexCount;

		const bool vertexOk = m_VertexFreeList.Allocate(vertexCount, allocation.VertexOffset);
		CORE_ASSERT(vertexOk, "GeometryPool: out of vertex space; no growth in this task -- see docs/tasks/T3-geometry-pool.md");
		if (!vertexOk)
		{
			NV_CORE_ERROR("[GeometryPool] Out of vertex space: requested {0} vertices, {1}/{2} used",
				vertexCount, m_VertexFreeList.UsedElements, m_VertexFreeList.Capacity);
			return {};
		}

		const bool indexOk = m_IndexFreeList.Allocate(indexCount, allocation.IndexOffset);
		CORE_ASSERT(indexOk, "GeometryPool: out of index space; no growth in this task -- see docs/tasks/T3-geometry-pool.md");
		if (!indexOk)
		{
			NV_CORE_ERROR("[GeometryPool] Out of index space: requested {0} indices, {1}/{2} used",
				indexCount, m_IndexFreeList.UsedElements, m_IndexFreeList.Capacity);
			m_VertexFreeList.Free(allocation.VertexOffset, vertexCount);
			return {};
		}

		return allocation;
	}

	void GeometryPool::Upload(
		const GeometryAllocation& allocation,
		const void* vertexData, uint32_t vertexDataSize,
		const void* indexData, uint32_t indexDataSize)
	{
		CORE_ASSERT(vertexDataSize == allocation.VertexCount * VertexStrideBytes, "GeometryPool::Upload vertex size mismatch");
		CORE_ASSERT(indexDataSize == allocation.IndexCount * static_cast<uint32_t>(sizeof(uint32_t)), "GeometryPool::Upload index size mismatch");

		GPUBuffer* vertexBuffer = ResourceManager::GetBuffer(m_VertexBuffer);
		GPUBuffer* indexBuffer = ResourceManager::GetBuffer(m_IndexBuffer);
		CORE_ASSERT(vertexBuffer && indexBuffer, "GeometryPool buffers are invalid");

		GPUBufferSpecification stagingSpec;
		stagingSpec.DebugName = "GeometryPool Staging";
		stagingSpec.MemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		stagingSpec.Size = vertexDataSize + indexDataSize;
		stagingSpec.Usage = GPUBufferUsage::StagingUsage;
		Handle<GPUBuffer> stagingHandle = ResourceManager::CreateBuffer(stagingSpec);
		GPUBuffer* staging = ResourceManager::GetBuffer(stagingHandle);

		ResourceAllocator allocator("GeometryPool Staging");
		uint8_t* dest = allocator.MapMemory<uint8_t>(staging->Allocation);
		std::memcpy(dest, vertexData, vertexDataSize);
		std::memcpy(dest + vertexDataSize, indexData, indexDataSize);
		allocator.UnmapMemory(staging->Allocation);

		TransientCommandBuffer copyCmd = Renderer::GetTransientCommandExecutor().BeginGraphics();
		TransferCommandList transfer(copyCmd.GetRecordingView());
		transfer.CopyBuffer(staging->Buffer, vertexBuffer->Buffer, vertexDataSize, 0,
			static_cast<VkDeviceSize>(allocation.VertexOffset) * VertexStrideBytes);
		transfer.CopyBuffer(staging->Buffer, indexBuffer->Buffer, indexDataSize, vertexDataSize,
			static_cast<VkDeviceSize>(allocation.IndexOffset) * sizeof(uint32_t));
		// SubmitAndWait's fence wait fully serializes this copy ahead of
		// anything recorded afterward -- see this method's header comment.
		copyCmd.SubmitAndWait();

		ResourceManager::DestroyBuffer(stagingHandle);
	}

	void GeometryPool::Free(const GeometryAllocation& allocation)
	{
		if (!allocation.IsValid())
			return;

		DeferredResourceDeletionQueue::SubmitCurrentFrame([this, allocation]()
		{
			m_VertexFreeList.Free(allocation.VertexOffset, allocation.VertexCount);
			m_IndexFreeList.Free(allocation.IndexOffset, allocation.IndexCount);
		});
	}

	uint64_t GeometryPool::GetVertexBufferAddress() const
	{
		GPUBuffer* buffer = ResourceManager::GetBuffer(m_VertexBuffer);
		CORE_ASSERT(buffer, "GeometryPool vertex buffer is invalid");
		return buffer ? static_cast<uint64_t>(buffer->GetDeviceAddress()) : 0;
	}

	uint64_t GeometryPool::GetIndexBufferAddress() const
	{
		GPUBuffer* buffer = ResourceManager::GetBuffer(m_IndexBuffer);
		CORE_ASSERT(buffer, "GeometryPool index buffer is invalid");
		return buffer ? static_cast<uint64_t>(buffer->GetDeviceAddress()) : 0;
	}

	GeometryPool::Stats GeometryPool::GetStats() const
	{
		Stats stats;
		stats.UsedVertexBytes = static_cast<uint64_t>(m_VertexFreeList.UsedElements) * VertexStrideBytes;
		stats.CapacityVertexBytes = static_cast<uint64_t>(m_VertexFreeList.Capacity) * VertexStrideBytes;
		stats.UsedIndexBytes = static_cast<uint64_t>(m_IndexFreeList.UsedElements) * sizeof(uint32_t);
		stats.CapacityIndexBytes = static_cast<uint64_t>(m_IndexFreeList.Capacity) * sizeof(uint32_t);
		return stats;
	}
}
