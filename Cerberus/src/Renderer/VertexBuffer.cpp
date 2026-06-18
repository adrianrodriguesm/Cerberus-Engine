#include "pch.h"
#include "VertexBuffer.h"
#include "Renderer/Renderer.h"
#include "Renderer/ResourceManager.h"

namespace cerberus
{
	VertexBuffer::VertexBuffer(const void* data, uint32_t size)
	{
		m_LocalData = Buffer::Copy(data, size);
		Invalidate();
	}
	VertexBuffer::VertexBuffer(uint32_t size)
	{
	}
	VertexBuffer::~VertexBuffer()
	{
		ResourceManager::DestroyBuffer(m_VertexBuffer);
		m_LocalData.Release();
	}

	VkBuffer VertexBuffer::GetHandle() const
	{
		GPUBuffer* buffer = ResourceManager::GetBuffer(m_VertexBuffer);
		CORE_ASSERT(buffer, "Invalid vertex buffer!")
		return buffer->Buffer;
	}

	void VertexBuffer::Invalidate()
	{
		GPUBufferSpecification stagingBufferSpec;
		stagingBufferSpec.DebugName = "Vertex Staging Buffer";
		stagingBufferSpec.MemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		stagingBufferSpec.Size = m_LocalData.GetSize();
		stagingBufferSpec.Usage = GPUBufferUsage::StagingUsage;
		Handle<GPUBuffer> stagingBufferHandle = ResourceManager::CreateBuffer(stagingBufferSpec);
		GPUBuffer* stagingBuffer = ResourceManager::GetBuffer(stagingBufferHandle);

		ResourceAllocator allocator("Vertex Staging Buffer");

		// Copy data to staging buffer
		// 1. Create pointer to a point in normal memory and  "Map" the staging buffer memory to that point
		uint8_t* destData = allocator.MapMemory<uint8_t>(stagingBuffer->Allocation);
		// 2. Copy memory from vertices vector to the point
		memcpy(destData,m_LocalData.Data, m_LocalData.Size);
		// 3. Unmap the vertex buffer memory
		allocator.UnmapMemory(stagingBuffer->Allocation);

		GPUBufferSpecification vertexBufferSpec;
		vertexBufferSpec.DebugName = "Vertex Buffer";
		vertexBufferSpec.MemoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		vertexBufferSpec.Size = m_LocalData.GetSize();
		vertexBufferSpec.Usage = GPUBufferUsage::VertexBufferUsage;
		m_VertexBuffer = ResourceManager::CreateBuffer(vertexBufferSpec);
		GPUBuffer* vertexBuffer = ResourceManager::GetBuffer(m_VertexBuffer);
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
		vkCmdCopyBuffer(copyCmd, stagingBuffer->Buffer, vertexBuffer->Buffer, 1, &copyRegion);
		// Flush command bufer (sumit to execution and detroy)
		copyCmd.SubmitAndWait();
		

		ResourceManager::DestroyBuffer(stagingBufferHandle);
	}


}
