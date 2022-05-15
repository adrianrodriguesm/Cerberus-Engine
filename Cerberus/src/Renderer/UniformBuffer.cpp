#include "pch.h"
#include "UniformBuffer.h"
#include "Renderer/Renderer.h"
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
		UniformBuffer* instance = this;
		Renderer::Submit([instance, offset] () mutable
		{
			VulkanAllocator allocator("UniformBuffer");
			// Copy data to staging buffer
			// 1. Create pointer to   a point in normal memory and  "Map" the uniform buffer memory to that point
			uint8_t* destData = allocator.MapMemory<uint8_t>(instance->m_MemoryAllocation);
			// 2. Copy memory to the point
			memcpy(destData, (byte*)instance->m_LocalData.Data + offset, instance->m_LocalData.Size);
			// 3. Unmap the uniform buffer memory
			allocator.UnmapMemory(instance->m_MemoryAllocation);
		});
	}
	void UniformBuffer::Release()
	{
		// In case of creating a uniform buffer and never set the data
		if (!m_MemoryAllocation)
			return;

		VmaAllocation memoryAllocation = m_MemoryAllocation;
		VkBuffer uniformBuffer = m_UniformBuffer;
		Renderer::SubmitResourceFree([uniformBuffer, memoryAllocation]()
		{
			VulkanAllocator allocator("UniformBuffer");
			NV_CORE_WARN("UniformBuffer::Release = {0}", (const void*)uniformBuffer);
			allocator.DestroyBuffer(uniformBuffer, memoryAllocation);
		});
		
		m_UniformBuffer = nullptr;
		m_MemoryAllocation = nullptr;

		m_LocalData.Release();
	
	}
	void UniformBuffer::Invalidate()
	{
		//Release();
		UniformBuffer* instance = this;
		Renderer::Submit([instance]() mutable
		{
			auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

			// Information to create a uniform buffer
			VkBufferCreateInfo uniformBufferCreateInfo = {};
			uniformBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			uniformBufferCreateInfo.size = (uint64_t)instance->m_Size;					// Size of buffer (size of 1 vertex * number of vertices)
			uniformBufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;			// Multiple types of buffer possible, we want Transfer Buffer 
																						// to sent the data to the Vertex buffer on the GPU
			uniformBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;			// Similar to Swap Chain images, can share uniform buffers

			VulkanAllocator allocator("UniformBuffer");
			// Allocation is the piece of memory associate with the buffer
			instance->m_MemoryAllocation = allocator.AllocateBuffer(uniformBufferCreateInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, instance->m_UniformBuffer);
			
			// Buffer info and data offset info
			instance->m_BufferDescriptorInfo = {};
			instance->m_BufferDescriptorInfo.buffer = instance->m_UniformBuffer;	// Buffer to get data from
			instance->m_BufferDescriptorInfo.offset = 0;							// Position of start of data
			instance->m_BufferDescriptorInfo.range = instance->m_AligmentData;		// Size of data
		});
	}
}