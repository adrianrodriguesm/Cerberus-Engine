#include "pch.h"
#include "IndexBuffer.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/Renderer.h"

namespace cerberus
{
	IndexBuffer::IndexBuffer(const void* data, const uint32_t size)
	{
		m_LocalData = Buffer::Copy(data, size);
		Invalidate();
	}
	IndexBuffer::~IndexBuffer()
	{
		ResourceManager::DestroyBuffer(m_IndexBuffer);
		m_LocalData.Release();
	}

	void IndexBuffer::Invalidate()
	{
		GPUBufferSpecification stagingBufferSpec;
		stagingBufferSpec.DebugName = "Index Staging Buffer";
		stagingBufferSpec.MemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		stagingBufferSpec.Size = m_LocalData.GetSize();
		stagingBufferSpec.Usage = GPUBufferUsage::StagingUsage;
		Handle<GPUBuffer> stagingBufferHandle = ResourceManager::CreateBuffer(stagingBufferSpec);
		GPUBuffer* stagingBuffer = ResourceManager::GetBuffer(stagingBufferHandle);
		ResourceAllocator allocator("Index Staging Buffer");

		// Copy data to staging buffer
		// 1. Create pointer to a point in normal memory and  "Map" the staging buffer memory to that point
		uint8_t* destData = allocator.MapMemory<uint8_t>(stagingBuffer->Allocation);
		// 2. Copy memory from vertices vector to the point
		memcpy(destData, m_LocalData.Data, m_LocalData.Size);
		// 3. Unmap the vertex buffer memory
		allocator.UnmapMemory(stagingBuffer->Allocation);

		GPUBufferSpecification indexBufferSpec;
		indexBufferSpec.DebugName = "Index Buffer";
		indexBufferSpec.MemoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		indexBufferSpec.Size = m_LocalData.GetSize();
		indexBufferSpec.Usage = GPUBufferUsage::IndexBufferUsage;
		m_IndexBuffer = ResourceManager::CreateBuffer(indexBufferSpec);
		
		GPUBuffer* indexBuffer = ResourceManager::GetBuffer(m_IndexBuffer);
		stagingBuffer = ResourceManager::GetBuffer(stagingBufferHandle);

		// Get a copy command buffer to tranfer the data 
		// from the staging buffer to the vertex buffer
		TransientCommandBuffer copyCmd = Renderer::BeginTransientCommandBuffer();

		// Region of data to copy from and to
		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = 0;			// Copy from the begining of the fisrt buffer
		copyRegion.dstOffset = 0;			// Copy from the begining of the destination buffer
		copyRegion.size = m_LocalData.Size;
		// Record copy operation to the command buffer
		vkCmdCopyBuffer(copyCmd, stagingBuffer->Buffer, indexBuffer->Buffer, 1, &copyRegion);
		// Flush command bufer (sumit to execution and detroy)
		copyCmd.SubmitAndWait();


		ResourceManager::DestroyBuffer(stagingBufferHandle);
	}
	VkBuffer IndexBuffer::GetHandle() const
	{
		GPUBuffer* buffer = ResourceManager::GetBuffer(m_IndexBuffer);
		CORE_ASSERT(buffer, "Invalid index buffer!")
		return buffer->Buffer;
	}
}
