#pragma once
#include "Renderer/ResourceAllocator.h"
#include "Core/Buffer.h"
#include "Core/Handle.h"
#include "Renderer/GPUBuffer.h"
namespace cerberus
{
	class IndexBuffer
	{
	public:
		IndexBuffer(const void* data, const uint32_t size);
		~IndexBuffer();

		uint32_t GetCount() { return m_LocalData.Size / sizeof(uint32_t); }

		void Invalidate();

		Handle<GPUBuffer> GetGPUBuffer() const { return m_IndexBuffer; }
		VkBuffer GetHandle() const;
	private:
		Handle<GPUBuffer> m_IndexBuffer;
		Buffer m_LocalData;
	};
}

