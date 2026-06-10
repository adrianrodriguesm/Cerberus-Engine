#include "pch.h"
#include "StorageBuffer.h"
#include "Renderer/Renderer.h"
#include "Renderer/ResourceManager.h"
namespace cerberus
{
	StorageBuffer::StorageBuffer(uint32_t size, uint32_t binding, BufferUsageType bufferUsage, uint64_t aligment)
		: m_BindingPoint(binding), m_Size(size), m_BufferUsageType(bufferUsage)
	{
		m_AligmentData = (m_BufferUsageType == BufferUsageType::Dynamic) ? aligment : m_Size;
		m_LocalData.Allocate(size);
		Invalidate();
	}
	StorageBuffer::~StorageBuffer()
	{
		Release();
	}

	void StorageBuffer::SetData(void* data, uint32_t size, uint32_t offset)
	{
		if (offset + size > m_Size)
		{
			Invalidate();
		}
		m_LocalData.Write(data, size, offset);
		GPUBuffer* buffer = ResourceManager::GetBuffer(m_StorageBuffer);
		ResourceAllocator allocator("StorageBuffer");
		// Copy data to staging buffer
		// 1. Create pointer to   a point in normal memory and  "Map" the uniform buffer memory to that point
		uint8_t* destData = allocator.MapMemory<uint8_t>(buffer->Allocation);
		// 2. Copy memory to the point
		memcpy(destData, (byte*)m_LocalData.Data + offset, m_LocalData.Size);
		// 3. Unmap the uniform buffer memory
		allocator.UnmapMemory(buffer->Allocation);
	}
	void StorageBuffer::Release()
	{
		if (m_Size > 0)
		{
			Handle<GPUBuffer> StorageBuffer = m_StorageBuffer;
			ResourceManager::DestroyBuffer(StorageBuffer);
			m_LocalData.Release();
		}

	}
	void StorageBuffer::Invalidate()
	{
		Release();

		auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		GPUBufferSpecification spec;
		spec.Usage = GPUBufferUsage::StorageBufferUsage;
		spec.Size = m_Size;
		spec.MemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		spec.DebugName = "StorageBuffer";

		m_StorageBuffer = ResourceManager::CreateBuffer(spec);
		GPUBuffer* buffer = ResourceManager::GetBuffer(m_StorageBuffer);
		// Buffer info and data offset info
		m_BufferDescriptorInfo = {};
		m_BufferDescriptorInfo.buffer = buffer->Buffer;				// Buffer to get data from
		m_BufferDescriptorInfo.offset = 0;							// Position of start of data
		m_BufferDescriptorInfo.range = m_AligmentData;				// Size of data
	}
}