#pragma once
#include "Renderer/Resources/BindGroup.h"
#include "Renderer/Resources/MeshProxy.h"
#include "Renderer/Shader.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Pipeline.h"
#include <span>
namespace cerberus
{
	namespace utils
	{
		inline uint32_t GetCountOfBits(uint32_t v)
		{
			uint32_t count = 0;
			while (v)
			{
				v &= v - 1;  // clear the least significant bit set
				++count;
			}
			return count;
		}
	}

	// 0 -> Global Perisistent data (env map)
	// 1 -> Global dynamic data (once per frame) (e.g shadow map, camera)
	// 2 -> Global dynamic data per render pass (once per frame)
	// 3 -> material data (once per draw)
	struct DrawArea
	{
		Handle<GraphicsPipeline> GraphicsPipeline;
		Handle<RayTracingPipeline> RayTracingPipeline;
		Handle<Framebuffer> Framebuffer;

		Handle<BindGroup> BindGroup0;
		Handle<BindGroup> BindGroup1;
		uint32_t DrawOffset = 0;
		uint32_t DrawCount = 0;

		bool ExplicitClear = false;
	};

	enum DrawStreamFlags : uint16_t
	{
		SHADER = BIT(1),
		
		VERTEX_BUFFER = BIT(2),
		VERTEX_OFFSET = BIT(3),
		
		INDEX_BUFFER = BIT(4),
		INDEX_OFFSET = BIT(5),
		INDEX_COUNT = BIT(6),

		BIND_GROUP_MAT = BIT(7)
	};

	struct DrawCallInfo
	{
		Handle<GPUBuffer> VertexBuffer, IndexBuffer;
		uint32_t VertexOffset, IndexOffset;
		uint32_t IndexCount;

		Handle<BindGroup> MaterialBindGoup;
	};


	class DrawStreamBuilder final
	{
	public:
		DrawStreamBuilder(uint32_t initialSize) : m_DirtyFlags() {}

		inline void SetVertexBuffer(const Handle<GPUBuffer> vertexBuffer)
		{
			if (vertexBuffer != m_Draw.VertexBuffer)
			{
				m_DirtyFlags |= DrawStreamFlags::VERTEX_BUFFER;
				m_Draw.VertexBuffer = vertexBuffer;
			}

		}

		inline void SetVertexBufferOffset(const uint32_t vertexBufferOffset)
		{
			if (vertexBufferOffset != m_Draw.VertexOffset)
			{
				m_DirtyFlags |= DrawStreamFlags::VERTEX_OFFSET;
				m_Draw.VertexOffset = vertexBufferOffset;
			}

		}

		void SetIndexBuffer(const Handle<GPUBuffer> indexBuffer)
		{
			if (indexBuffer != m_Draw.IndexBuffer)
			{
				m_DirtyFlags |= DrawStreamFlags::INDEX_BUFFER;
				m_Draw.IndexBuffer = indexBuffer;
			}
		}

		void SetIndexBufferOffset(const uint32_t indexBufferOffset)
		{
			if (indexBufferOffset != m_Draw.IndexOffset)
			{
				m_DirtyFlags |= DrawStreamFlags::INDEX_OFFSET;
				m_Draw.IndexOffset = indexBufferOffset;
			}
		}

		void SetIndexBufferCount(const uint32_t indexBufferCount)
		{
			if (indexBufferCount != m_Draw.IndexCount)
			{
				m_DirtyFlags |= DrawStreamFlags::INDEX_COUNT;
				m_Draw.IndexCount = indexBufferCount;
			}
		}

		void SetBindGroupMaterial(const Handle<BindGroup> matBindGoup)
		{
			if (matBindGoup != m_Draw.MaterialBindGoup)
			{
				m_DirtyFlags |= DrawStreamFlags::BIND_GROUP_MAT;
				m_Draw.MaterialBindGoup = matBindGoup;
			}
		}


		std::vector<uint32_t> GetStream() const { return m_Stream; }

		inline void Draw()
		{
			uint32_t dirtyCount = utils::GetCountOfBits(m_DirtyFlags);
			uint32_t offset = (uint32_t)m_Stream.size();
			m_Stream.resize(offset + dirtyCount + 1);
			m_Stream[offset++] = (uint32_t)m_DirtyFlags;
			if (m_DirtyFlags & DrawStreamFlags::VERTEX_BUFFER)   m_Stream[offset++] = *((uint32_t*)&m_Draw.VertexBuffer);
			if (m_DirtyFlags & DrawStreamFlags::INDEX_BUFFER)    m_Stream[offset++] = *((uint32_t*)&m_Draw.IndexBuffer);
			if (m_DirtyFlags & DrawStreamFlags::VERTEX_OFFSET)   m_Stream[offset++] = *((uint32_t*)&m_Draw.VertexOffset);
			if (m_DirtyFlags & DrawStreamFlags::INDEX_OFFSET)    m_Stream[offset++] = *((uint32_t*)&m_Draw.IndexOffset);
			if (m_DirtyFlags & DrawStreamFlags::INDEX_COUNT)     m_Stream[offset++] = *((uint32_t*)&m_Draw.IndexCount);
			//if (m_DirtyFlags & DrawStreamFlags::BIND_GROUP_MAT)  m_Stream[offset++] = *((uint32_t*)&m_Draw.MaterialBindGoup);

			m_DirtyFlags = 0;
		}

	private:
		std::vector<uint32_t> m_Stream;
		DrawCallInfo m_Draw;
		uint16_t m_DirtyFlags;
	};
}