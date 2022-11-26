#pragma once
#include "Renderer/ResourceAllocator.h"
#include "Core/Buffer.h"
namespace cerberus
{
	class IndexBuffer
	{
	public:
		IndexBuffer(const void* data, const uint32_t size);
		~IndexBuffer();

		uint32_t GetCount() const { return m_Size / sizeof(uint32_t); }

		uint32_t GetSize() const { return m_Size; }

		VkBuffer GetVulkanBuffer() { return m_IndexBuffer; }
	private:
		VkBuffer m_IndexBuffer;
		uint32_t m_Size;
		Buffer m_LocalData;
		VmaAllocation m_MemoryAllocation;
	};
}

