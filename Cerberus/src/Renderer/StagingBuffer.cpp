#include "pch.h"
#include "StagingBuffer.h"
#include "ResourceManager.h"
#include "Renderer/Renderer.h"

namespace cerberus
{
	StagingBuffer::StagingBuffer(uint32_t size, bool hostToDevice)
	{
		GPUBufferSpecification spec;
		spec.MemoryUsage = hostToDevice ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_TO_CPU;
		spec.Usage = GPUBufferUsage::StagingUsage;
		spec.Size = size;

		m_GPUBuffer = ResourceManager::CreateBuffer(spec);
	}

	StagingBuffer::~StagingBuffer()
	{
		ResourceManager::DestroyBuffer(m_GPUBuffer);
	}

	void StagingBuffer::CopyToBuffer(Handle<GPUBuffer> buffer, VkDeviceSize dstOffset, VkDeviceSize size, const void* data)
	{
		GPUBuffer* bufferToCopy = ResourceManager::GetBuffer(buffer);
		CORE_ASSERT(bufferToCopy, "Invalid bufffer");
		GPUBuffer* stagingBuffer = ResourceManager::GetBuffer(m_GPUBuffer);
		CORE_ASSERT(stagingBuffer, "Invalid staging bufffer");
		CORE_ASSERT(size <= stagingBuffer->Size, "Size if bigger than staging buffer size!");

		ResourceAllocator allocator("Staging Buffer Copy");
		// Copy data to staging buffer
		// 1. Create pointer to a point in normal memory and  "Map" the staging buffer memory to that point
		uint8_t* destData = allocator.MapMemory<uint8_t>(stagingBuffer->Allocation);
		// 2. Copy memory from vertices vector to the point
		memcpy(destData, data, size);
		// 3. Unmap the vertex buffer memory
		allocator.UnmapMemory(stagingBuffer->Allocation);

		// Get a copy command buffer to tranfer the data 
		// from the staging buffer to the vertex buffer
		TransientCommandBuffer copyCmd = Renderer::GetTransientCommandExecutor().BeginGraphics();

		// Region of data to copy from and to
		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = 0;			// Copy from the begining of the fisrt buffer
		copyRegion.dstOffset = dstOffset;	// Copy to the offset of the destination buffer
		copyRegion.size = size;
		// Record copy operation to the command buffer
		vkCmdCopyBuffer(copyCmd, stagingBuffer->Buffer, bufferToCopy->Buffer, 1, &copyRegion);
		// Flush command bufer (sumit to execution and detroy)
		copyCmd.SubmitAndWait();
	}
}
