#pragma once
#include "Core/Base.h"
#include "VulkanMemoryAllocator/vk_mem_alloc.h"
#include "Renderer/RHI/Device.h"
namespace cerberus
{
	struct GPUMemoryStats
	{
		uint64_t Used = 0;
		uint64_t Free = 0;
	};

	class ResourceAllocator
	{
	public:
		ResourceAllocator() = default;
		ResourceAllocator(const std::string& tag);
		~ResourceAllocator();

		VmaAllocation AllocateBuffer(VkBufferCreateInfo bufferCreateInfo, VmaMemoryUsage usage, VkBuffer& outBuffer);
		VmaAllocation AllocateBuffer(VkBufferCreateInfo bufferCreateInfo, VmaMemoryUsage usage, VmaAllocationCreateFlags flags, VkBuffer& outBuffer);
		VmaAllocation AllocateImage(VkImageCreateInfo imageCreateInfo, VmaMemoryUsage usage, VkImage& outImage);

		void DestroyBuffer(VkBuffer buffer, VmaAllocation allocation);
		void DestroyImage(VkImage image, VmaAllocation allocation);

		void Free(VmaAllocation allocation);


		template<typename T>
		T* MapMemory(VmaAllocation allocation)
		{
			T* mappedMemory;
			vmaMapMemory(ResourceAllocator::GetVMAAllocator(), allocation, (void**)&mappedMemory);
			return mappedMemory;
		}

		void UnmapMemory(VmaAllocation allocation);

		
		static GPUMemoryStats GetStats();
		static void Init(Ref<Device> device);
		static VmaAllocator& GetVMAAllocator();
		static void Shutdown();
	private:
		std::string m_Tag;
	};
}
