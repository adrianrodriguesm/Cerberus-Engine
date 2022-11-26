#include "pch.h"
#include "IndexBuffer.h"
#include "Renderer/Renderer.h"

namespace cerberus
{
	IndexBuffer::IndexBuffer(const void* data, const uint32_t size)
		: m_Size(size)
	{
		m_LocalData = Buffer::Copy(data, size);
		IndexBuffer* instance = this;
		Renderer::Submit([instance]() mutable
		{
			auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			// CREATE STAGING BUFFER
			// Information to create a buffer (doesn't include assigning memory)
			VkBufferCreateInfo stagingBufferCreateInfo = {};
			stagingBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			stagingBufferCreateInfo.size = instance->m_Size;							// Size of buffer (size of 1 vertex * number of vertices)
			stagingBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;			// Multiple types of buffer possible, we want Transfer Buffer 
																						// to sent the data to the Vertex buffer on the GPU
			stagingBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;			// Similar to Swap Chain images, can share vertex buffers

			VkBuffer stagingBuffer;
			ResourceAllocator allocator("IndexBuffer");
			// Allocation is the piece of memory associate with the buffer
			VmaAllocation stagingBufferAllocation = allocator.AllocateBuffer(stagingBufferCreateInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer);

			// Copy data to staging buffer
			// 1. Create pointer to a point in normal memory and  "Map" the staging buffer memory to that point
			uint8_t* destData = allocator.MapMemory<uint8_t>(stagingBufferAllocation);
			// 2. Copy memory from vertices vector to the point
			memcpy(destData, instance->m_LocalData.Data, instance->m_LocalData.Size);
			// 3. Unmap the vertex buffer memory
			allocator.UnmapMemory(stagingBufferAllocation);

			// CREATE VERTEX BUFFER
			VkBufferCreateInfo vertexBufferCreateInfo = {};
			vertexBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			vertexBufferCreateInfo.size = instance->m_Size;
			// VK_BUFFER_USAGE_TRANSFER_DST_BIT : will receive data from another tranfer source (staging buffer)
			vertexBufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			vertexBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			// Create Vertex buffer
			instance->m_MemoryAllocation = allocator.AllocateBuffer(vertexBufferCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY, VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, instance->m_IndexBuffer);

			// Get a copy command buffer to tranfer the data 
			// from the staging buffer to the vertex buffer
			VkCommandBuffer copyCmd = Renderer::GetCommandBuffer(true);
			
			// Region of data to copy from and to
			VkBufferCopy copyRegion = {};
			copyRegion.srcOffset = 0;			// Copy from the begining of the fisrt buffer
			copyRegion.dstOffset = 0;			// Copy from the begining of the destination buffer
			copyRegion.size = instance->m_LocalData.Size;
			// Record copy operation to the command buffer
			vkCmdCopyBuffer(copyCmd, stagingBuffer, instance->m_IndexBuffer, 1, &copyRegion);
			// Flush command bufer (sumit to execution and detroy)
			Renderer::FlushCommandBuffer(copyCmd);

			// Destroy staging buffer not longer need it
			allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);
		});

	}
	IndexBuffer::~IndexBuffer()
	{
		VkBuffer indexBuffer = m_IndexBuffer;
		VmaAllocation memoryAllocation = m_MemoryAllocation;
		Renderer::SubmitResourceFree([indexBuffer, memoryAllocation]() 
		{
			ResourceAllocator allocator("IndexBuffer");
			allocator.DestroyBuffer(indexBuffer, memoryAllocation);
		});
		
		m_LocalData.Release();
		m_MemoryAllocation = nullptr;
		m_IndexBuffer = nullptr;
	}
}
