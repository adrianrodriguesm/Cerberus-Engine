#pragma once
#include "Renderer/Descriptor/BindGroup.h"
#include "Renderer/Scene/Proxies/MeshProxy.h"
#include "Renderer/Shader/Shader.h"
#include "Renderer/RHI/Pipeline/Pipeline.h"
#include <cstring>
#include <span>
#include <type_traits>
namespace cerberus
{
	// 0 -> Global Perisistent data (env map)
	// 1 -> Global dynamic data (once per frame) (e.g shadow map, camera)
	// 2 -> Global dynamic data per render pass (once per frame)
	// 3 -> material data (once per draw)
	struct DrawArea
	{
		Handle<GraphicsPipeline> GraphicsPipeline;
		Handle<RayTracingPipeline> RayTracingPipeline;

		Handle<BindGroup> BindGroup0;
		Handle<BindGroup> BindGroup1;
		// Word offset into the encoded draw stream. The stream uses uint32_t words.
		uint32_t DrawOffset = 0;
		uint32_t DrawCount = 0;
	};

	enum DrawStreamFlags : uint16_t
	{
		SHADER = BIT(1),

		VERTEX_BUFFER = BIT(2),
		VERTEX_OFFSET = BIT(3),

		INDEX_BUFFER = BIT(4),
		INDEX_OFFSET = BIT(5),
		INDEX_COUNT = BIT(6),

		// Bindless material index, pushed as a constant into the already-bound
		// geometry bind group's material table. Every consumer (PreDepth, Shadow,
		// Forward) binds the same set of geometry buffers; only pipelines that
		// actually shade (Forward) declare a push-constant range wide enough to
		// read this field.
		MATERIAL_INDEX = BIT(7),
		// Per-instance world transform. Differs on essentially every draw, so
		// SetWorldTransform() always marks this dirty rather than diffing.
		WORLD_TRANSFORM = BIT(8),
	};

	struct DrawCallInfo
	{
		Handle<GPUBuffer> VertexBuffer, IndexBuffer;
		uint32_t VertexOffset = 0;
		uint32_t IndexOffset = 0;
		uint32_t IndexCount = 0;

		glm::mat4 WorldTransform = glm::mat4(1.0f);
		uint32_t MaterialIndex = 0;
	};

	// Reads the uint32_t-word stream produced by DrawStreamBuilder. Values are
	// copied byte-for-byte so 64-bit generational handles keep both their index
	// and generation without relying on pointer casts or strict-aliasing UB.
	class DrawStreamReader final
	{
	public:
		explicit DrawStreamReader(std::span<const uint32_t> stream)
			: m_Stream(stream)
		{
		}

		template<typename T>
		T Read()
		{
			static_assert(std::is_trivially_copyable_v<T>, "Draw stream values must be trivially copyable");

			constexpr size_t wordCount = (sizeof(T) + sizeof(uint32_t) - 1) / sizeof(uint32_t);
			CORE_ASSERT(m_Cursor + wordCount <= m_Stream.size(), "Malformed draw stream");

			T value{};
			std::memcpy(&value, m_Stream.data() + m_Cursor, sizeof(T));
			m_Cursor += wordCount;
			return value;
		}

		bool Empty() const { return m_Cursor == m_Stream.size(); }
		size_t GetCursor() const { return m_Cursor; }

	private:
		std::span<const uint32_t> m_Stream;
		size_t m_Cursor = 0;
	};

	class DrawStreamBuilder final
	{
	public:
		explicit DrawStreamBuilder(uint32_t initialSize = 0)
		{
			m_Stream.reserve(initialSize);
		}

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

		// A mesh instance's world transform is essentially unique per draw, so this
		// always marks the stream dirty instead of diffing against the last value.
		void SetWorldTransform(const glm::mat4& worldTransform)
		{
			m_DirtyFlags |= DrawStreamFlags::WORLD_TRANSFORM;
			m_Draw.WorldTransform = worldTransform;
		}

		void SetMaterialIndex(const uint32_t materialIndex)
		{
			if (materialIndex != m_Draw.MaterialIndex)
			{
				m_DirtyFlags |= DrawStreamFlags::MATERIAL_INDEX;
				m_Draw.MaterialIndex = materialIndex;
			}
		}


		std::span<const uint32_t> GetStream() const { return m_Stream; }
		uint32_t GetDrawCount() const { return m_DrawCount; }

		// Starts a self-contained stream range from the current state. The first
		// Draw() after this call re-emits every state value required by a decoder
		// that begins at the returned word offset.
		uint32_t BeginDrawArea()
		{
			m_DirtyFlags = 0;
			if (m_Draw.VertexBuffer.IsValid())
				m_DirtyFlags |= DrawStreamFlags::VERTEX_BUFFER;
			if (m_Draw.IndexBuffer.IsValid())
				m_DirtyFlags |= DrawStreamFlags::INDEX_BUFFER;
			if (m_Draw.VertexOffset != 0)
				m_DirtyFlags |= DrawStreamFlags::VERTEX_OFFSET;
			if (m_Draw.IndexOffset != 0)
				m_DirtyFlags |= DrawStreamFlags::INDEX_OFFSET;
			if (m_Draw.IndexCount != 0)
				m_DirtyFlags |= DrawStreamFlags::INDEX_COUNT;
			m_DirtyFlags |= DrawStreamFlags::WORLD_TRANSFORM;
			m_DirtyFlags |= DrawStreamFlags::MATERIAL_INDEX;

			return static_cast<uint32_t>(m_Stream.size());
		}

		void Clear()
		{
			m_Stream.clear();
			m_DrawCount = 0;
			m_Draw = {};
			m_DirtyFlags = 0;
		}

		inline void Draw()
		{
			Append(m_DirtyFlags);
			if (m_DirtyFlags & DrawStreamFlags::VERTEX_BUFFER)   Append(m_Draw.VertexBuffer);
			if (m_DirtyFlags & DrawStreamFlags::INDEX_BUFFER)    Append(m_Draw.IndexBuffer);
			if (m_DirtyFlags & DrawStreamFlags::VERTEX_OFFSET)   Append(m_Draw.VertexOffset);
			if (m_DirtyFlags & DrawStreamFlags::INDEX_OFFSET)    Append(m_Draw.IndexOffset);
			if (m_DirtyFlags & DrawStreamFlags::INDEX_COUNT)     Append(m_Draw.IndexCount);
			if (m_DirtyFlags & DrawStreamFlags::WORLD_TRANSFORM) Append(m_Draw.WorldTransform);
			if (m_DirtyFlags & DrawStreamFlags::MATERIAL_INDEX)  Append(m_Draw.MaterialIndex);

			m_DirtyFlags = 0;
			m_DrawCount++;
		}

	private:
		template<typename T>
		void Append(const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>, "Draw stream values must be trivially copyable");

			constexpr size_t wordCount = (sizeof(T) + sizeof(uint32_t) - 1) / sizeof(uint32_t);
			const size_t offset = m_Stream.size();
			m_Stream.resize(offset + wordCount, 0);
			std::memcpy(m_Stream.data() + offset, &value, sizeof(T));
		}

		std::vector<uint32_t> m_Stream;
		DrawCallInfo m_Draw{};
		uint32_t m_DirtyFlags = 0;
		uint32_t m_DrawCount = 0;
	};
}
