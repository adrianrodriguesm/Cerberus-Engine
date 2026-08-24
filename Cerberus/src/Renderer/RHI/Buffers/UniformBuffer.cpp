#include "pch.h"
#include "UniformBuffer.h"
#include "Renderer/Core/Renderer.h"
#include "Renderer/RHI/ResourceManager.h"
namespace cerberus
{
	UniformBuffer::UniformBuffer(uint32_t size, uint32_t binding, BufferUsageType bufferUsage, uint64_t aligment)
		: m_BindingPoint(binding), m_Size(size), m_BufferUsageType(bufferUsage)
	{
		m_AligmentData = (m_BufferUsageType == BufferUsageType::Dynamic) ? aligment : m_Size;
		m_LocalData.Allocate(size);
		Invalidate();
	}
	UniformBuffer::~UniformBuffer()
	{
		Release();
	}
	
	void UniformBuffer::SetData(void* data, uint32_t size, uint32_t offset)
	{
		m_LocalData.Write(data, size, offset);
		GPUBuffer* buffer = ResourceManager::GetBuffer(m_UniformBuffer);
		ResourceAllocator allocator("UniformBuffer");
		// Copy data to staging buffer
		// 1. Create pointer to   a point in normal memory and  "Map" the uniform buffer memory to that point
		uint8_t* destData = allocator.MapMemory<uint8_t>(buffer->Allocation);
		// 2. Copy memory to the point
		memcpy(destData, (byte*)m_LocalData.Data + offset, m_LocalData.Size);
		// 3. Unmap the uniform buffer memory
		allocator.UnmapMemory(buffer->Allocation);
	}
	void UniformBuffer::Release()
	{
		Handle<GPUBuffer> uniformBuffer = m_UniformBuffer;
		ResourceManager::DestroyBuffer(uniformBuffer);
		m_LocalData.Release();
	
	}
	void UniformBuffer::Invalidate()
	{
		auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		GPUBufferSpecification spec;
		spec.Usage = GPUBufferUsage::UniformBufferUsage;
		spec.Size = m_Size;
		spec.MemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		spec.DebugName = "UniformBuffer";

		m_UniformBuffer = ResourceManager::CreateBuffer(spec);
		GPUBuffer* buffer = ResourceManager::GetBuffer(m_UniformBuffer);
		// Buffer info and data offset info
		m_BufferDescriptorInfo = {};
		m_BufferDescriptorInfo.buffer = buffer->Buffer;				// Buffer to get data from
		m_BufferDescriptorInfo.offset = 0;							// Position of start of data
		m_BufferDescriptorInfo.range = m_AligmentData;				// Size of data
	}
}
