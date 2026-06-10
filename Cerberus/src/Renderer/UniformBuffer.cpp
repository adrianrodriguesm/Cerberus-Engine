#include "pch.h"
#include "UniformBuffer.h"
#include "Renderer/Renderer.h"
#include "Renderer/ResourceManager.h"
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

#if OLD_RENDERER
		UniformBuffer* instance = this;
		Renderer::Submit([instance, offset] () mutable
		{
			GPUBuffer* buffer = ResourceManager::GetBuffer(instance->m_UniformBuffer);
			ResourceAllocator allocator("UniformBuffer");
			// Copy data to staging buffer
			// 1. Create pointer to   a point in normal memory and  "Map" the uniform buffer memory to that point
			uint8_t* destData = allocator.MapMemory<uint8_t>(buffer->Allocation);
			// 2. Copy memory to the point
			memcpy(destData, (byte*)instance->m_LocalData.Data + offset, instance->m_LocalData.Size);
			// 3. Unmap the uniform buffer memory
			allocator.UnmapMemory(buffer->Allocation);
		});
#endif
	}
	void UniformBuffer::Release()
	{
		Handle<GPUBuffer> uniformBuffer = m_UniformBuffer;
		ResourceManager::DestroyBuffer(uniformBuffer);
#if OLD_RENDERER
		Renderer::SubmitResourceFree([this]()
		{
			Handle<GPUBuffer> uniformBuffer = m_UniformBuffer;
			ResourceManager::DestroyBuffer(uniformBuffer);
		});
#endif
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
#if OLD_RENDERER
		//Release();
		UniformBuffer* instance = this;
		Renderer::Submit([instance]() mutable
		{
			auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

			GPUBufferSpecification spec;
			spec.Usage = GPUBufferUsage::UniformBufferUsage;
			spec.Size = instance->m_Size;
			spec.MemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
			spec.DebugName = "UniformBuffer";

			instance->m_UniformBuffer = ResourceManager::CreateBuffer(spec);
			GPUBuffer* buffer = ResourceManager::GetBuffer(instance->m_UniformBuffer);
			// Buffer info and data offset info
			instance->m_BufferDescriptorInfo = {};
			instance->m_BufferDescriptorInfo.buffer = buffer->Buffer;				// Buffer to get data from
			instance->m_BufferDescriptorInfo.offset = 0;							// Position of start of data
			instance->m_BufferDescriptorInfo.range = instance->m_AligmentData;		// Size of data
		});
#endif
	}
}