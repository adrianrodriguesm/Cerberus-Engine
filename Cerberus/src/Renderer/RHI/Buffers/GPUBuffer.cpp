#include "pch.h"
#include "GPUBuffer.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Renderer/Memory/DeferredResourceDeletionQueue.h"
namespace cerberus
{
	namespace utils
	{
		inline VkBufferUsageFlags CerberusToVulkanBufferUsage(const GPUBufferUsage& usage)
		{
			switch (usage)
			{
			case GPUBufferUsage::IndexBufferUsage:
				return VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			case GPUBufferUsage::VertexBufferUsage:
				return VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			case GPUBufferUsage::ASBufferUsage:
				return VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			case GPUBufferUsage::StagingUsage:
				return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			case GPUBufferUsage::UniformBufferUsage:
				return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			case GPUBufferUsage::StorageBufferUsage:
				return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			case GPUBufferUsage::ShaderBindingTableUsage:
				return VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
			default:
				CORE_ASSERT(false, "Unknow buffer usage type");
				break;
			}
			return 0;
		}
	}

	GPUBuffer::GPUBuffer(const GPUBufferSpecification& bufferSpec)
		: Size(bufferSpec.Size), DebugName(bufferSpec.DebugName)
	{
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

	GPUBuffer::GPUBuffer(GPUBuffer&& other) noexcept
		: Buffer(other.Buffer)
		, Size(other.Size)
		, Allocation(other.Allocation)
		, DebugName(std::move(other.DebugName))
	{
		other.Buffer = VK_NULL_HANDLE;
		other.Allocation = VK_NULL_HANDLE;
	}

	GPUBuffer::~GPUBuffer()
	{
		if (!Buffer || !Allocation)
		{
			return;
		}

		DeferredResourceDeletionQueue::SubmitCurrentFrame([name = DebugName, buffer = Buffer, allocation = Allocation]()
		{
			ResourceAllocator allocator(name);
			allocator.DestroyBuffer(buffer, allocation);
		});
		
		Buffer = VK_NULL_HANDLE;
		Allocation = VK_NULL_HANDLE;
	}

	VkDeviceAddress GPUBuffer::GetDeviceAddress() const
	{
		VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		info.buffer = Buffer;
		return vkGetBufferDeviceAddress(RendererContext::GetCurrentDevice()->GetVulkanDevice(), &info);
	}
}
