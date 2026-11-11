#pragma once
#include "Renderer/RHI/Buffers/GPUBuffer.h"
#include "Core/Handle.h"

#include <vector>

namespace cerberus
{
	// One sub-allocation out of a GeometryPool's vertex/index buffers. Offsets
	// are in elements (vertices / individual indices), not bytes -- directly
	// usable as vertex-pulling offsets (gl_VertexIndex already includes a
	// vkCmdDrawIndexed vertexOffset) and as a BLAS build's firstVertex /
	// primitiveOffset. See docs/tasks/T3-geometry-pool.md.
	struct GeometryAllocation
	{
		uint32_t VertexOffset = 0;
		uint32_t VertexCount = 0;
		uint32_t IndexOffset = 0;
		uint32_t IndexCount = 0;

		bool IsValid() const { return VertexCount > 0; }
	};

	// Global vertex/index buffer pool, one per RenderScene. Replaces one
	// VertexBuffer + IndexBuffer per MeshSourceProxy with sub-allocations out
	// of two large, fixed-capacity buffers, so every mesh is addressable (and
	// vertex-pullable) through the same base buffer device address instead of
	// a per-draw buffer bind.
	//
	// Fixed capacity, no growth (T3 scope -- see docs/tasks/T3-geometry-pool.md).
	// The vendored VMA predates virtual blocks (vmaCreateVirtualBlock, added in
	// VMA 3.0), so sub-allocation is a first-fit free list with coalescing
	// instead of vmaVirtualAllocate.
	class GeometryPool
	{
	public:
		// MeshSource::Vertex / Geometry.glsl's CERBERUS_VERTEX_STRIDE_FLOATS:
		// position, normal, tangent, binormal, uv -- 14 floats, interleaved.
		static constexpr uint32_t VertexStrideBytes = 14u * sizeof(float);

		static constexpr uint32_t DefaultVertexCapacityBytes = 256u * 1024u * 1024u;
		static constexpr uint32_t DefaultIndexCapacityBytes = 128u * 1024u * 1024u;

		explicit GeometryPool(
			uint32_t vertexCapacityBytes = DefaultVertexCapacityBytes,
			uint32_t indexCapacityBytes = DefaultIndexCapacityBytes);
		~GeometryPool();

		GeometryPool(const GeometryPool&) = delete;
		GeometryPool& operator=(const GeometryPool&) = delete;

		// Asserts and logs clearly on overflow -- no growth in this task.
		GeometryAllocation Allocate(uint32_t vertexCount, uint32_t indexCount);

		// Staging + TransferCommandList::CopyBuffer through the transient
		// executor's SubmitAndWait(), exactly like VertexBuffer::Invalidate()
		// did per-mesh before this -- no explicit post-copy barrier, since
		// SubmitAndWait's fence wait already fully serializes the copy ahead
		// of anything recorded afterward (same assumption every other
		// transient upload in this engine already relies on). vertexDataSize/
		// indexDataSize are in bytes and must match allocation's counts.
		void Upload(
			const GeometryAllocation& allocation,
			const void* vertexData, uint32_t vertexDataSize,
			const void* indexData, uint32_t indexDataSize);

		// Deferred via DeferredResourceDeletionQueue: the region only returns
		// to the free list once every frame-in-flight that could still be
		// reading it has retired, so it's never reused out from under an
		// in-flight draw or BLAS build.
		void Free(const GeometryAllocation& allocation);

		Handle<GPUBuffer> GetVertexBuffer() const { return m_VertexBuffer; }
		Handle<GPUBuffer> GetIndexBuffer() const { return m_IndexBuffer; }
		uint64_t GetVertexBufferAddress() const;
		uint64_t GetIndexBufferAddress() const;

		struct Stats
		{
			uint64_t UsedVertexBytes = 0, CapacityVertexBytes = 0;
			uint64_t UsedIndexBytes = 0, CapacityIndexBytes = 0;
		};
		Stats GetStats() const;

	private:
		struct FreeRegion
		{
			uint32_t Offset;
			uint32_t Count;
		};

		// First-fit-with-coalescing free list, shared by the vertex and index
		// pools -- they differ only in element size/capacity, not in how
		// regions are tracked.
		struct FreeList
		{
			std::vector<FreeRegion> Regions;
			uint32_t Capacity = 0;
			uint32_t UsedElements = 0;

			void Init(uint32_t capacity);
			bool Allocate(uint32_t count, uint32_t& outOffset);
			void Free(uint32_t offset, uint32_t count);
		};

		FreeList m_VertexFreeList;
		FreeList m_IndexFreeList;
		Handle<GPUBuffer> m_VertexBuffer;
		Handle<GPUBuffer> m_IndexBuffer;
	};
}
