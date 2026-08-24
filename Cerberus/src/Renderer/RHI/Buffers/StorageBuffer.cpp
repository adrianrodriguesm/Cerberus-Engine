#include "pch.h"
#include "StorageBuffer.h"
#include "Renderer/Core/Renderer.h"
#include "Renderer/RHI/ResourceManager.h"
#include <algorithm>
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

	void StorageBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
	{
		const uint32_t requiredSize = offset + size;
		if (requiredSize > m_Size)
		{
			Buffer resizedData;
			resizedData.Allocate(requiredSize);
			resizedData.ZeroInitialize();

			if (m_LocalData.Data && m_LocalData.Size > 0)
				memcpy(resizedData.Data, m_LocalData.Data, std::min(m_LocalData.Size, requiredSize));

			m_LocalData.Release();
			m_LocalData = resizedData;

			m_Size = requiredSize;
			m_AligmentData = (m_BufferUsageType == BufferUsageType::Dynamic) ? m_AligmentData : m_Size;
			Invalidate();
		}
		else if (!m_LocalData.Data)
		{
			m_LocalData.Allocate(m_Size);
			m_LocalData.ZeroInitialize();
		}

		m_LocalData.Write(data, size, offset);

		GPUBuffer* buffer = ResourceManager::GetBuffer(m_StorageBuffer);
		CORE_ASSERT(buffer, "Invalid StorageBuffer GPU buffer");

		ResourceAllocator allocator("StorageBuffer");
		// Copy data to staging buffer
		// 1. Create pointer to a point in normal memory and "Map" the storage buffer memory to that point
		uint8_t* destData = allocator.MapMemory<uint8_t>(buffer->Allocation);
		// 2. Copy memory to the point
		memcpy(destData, m_LocalData.Data, m_LocalData.Size);
		// 3. Unmap the storage buffer memory
		allocator.UnmapMemory(buffer->Allocation);
	}
	void StorageBuffer::Release()
	{
		if (m_StorageBuffer.IsValid())
		{
			Handle<GPUBuffer> storageBuffer = m_StorageBuffer;
			ResourceManager::DestroyBuffer(storageBuffer);
			m_StorageBuffer = {};
		}

		m_LocalData.Release();
	}
	void StorageBuffer::Invalidate()
	{
		if (m_StorageBuffer.IsValid())
		{
			Handle<GPUBuffer> storageBuffer = m_StorageBuffer;
			ResourceManager::DestroyBuffer(storageBuffer);
			m_StorageBuffer = {};
		}

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
