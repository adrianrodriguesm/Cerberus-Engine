#include "pch.h"
#include "GPUBuffer.h"

namespace cerberus
{
	namespace utils
	{
		inline VkBufferUsageFlags CerberusToVulkanBufferUsage(const GPUBufferUsage& usage)
		{
			switch (usage)
			{
			case GPUBufferUsage::IndexBufferUsage:
				return VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			case GPUBufferUsage::VertexBufferUsage:
				return VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			case GPUBufferUsage::ASBufferUsage:
				return VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			case GPUBufferUsage::StagingUsage:
				return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			}
			return 0;
		}
	}

	GPUBuffer::GPUBuffer(const GPUBufferSpecification& bufferSpec)
		: Size(bufferSpec.Size), DebugName(bufferSpec.DebugName)
	{
		//strcpy(DebugName, bufferSpec.DebugName.c_str());
		// Information to create a buffer (doesn't include assigning memory)
		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = bufferSpec.Size;											// Size of buffer (size of 1 vertex * number of vertices)
		bufferCreateInfo.usage = utils::CerberusToVulkanBufferUsage(bufferSpec.Usage);		// Multiple types of buffer possible, we want Transfer Buffer 
		// to sent the data to the Vertex buffer on the GPU
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;							// Similar to Swap Chain images, can share vertex buffers

		// Allocate and create the actual buffer
		ResourceAllocator allocator(DebugName);
		Allocation = allocator.AllocateBuffer(bufferCreateInfo, bufferSpec.MemoryUsage, Buffer);
	}

	GPUBuffer::~GPUBuffer()
	{
		if (Buffer)
		{
			ResourceAllocator allocator(DebugName);
			allocator.DestroyBuffer(Buffer, Allocation);
			Buffer = nullptr;
		}
	}
}