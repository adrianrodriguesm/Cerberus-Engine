#include "pch.h"
#include "ResourceAllocator.h"
#include "Renderer/RendererContext.h"
#include "Utils/StringUtils.h"

#define LOG_RENDERER_ALLOCATIONS 1

#if LOG_RENDERER_ALLOCATIONS
#define ALLOCATOR_LOG(...) NV_CORE_TRACE(__VA_ARGS__)
#else
#define ALLOCATOR_LOG(...)
#endif

namespace cerberus
{


	struct ResourceAllocatorData
	{
		VmaAllocator Allocator;
		uint64_t TotalAllocatedBytes = 0;
	};

	static ResourceAllocatorData* s_Data = nullptr;

	void ResourceAllocator::Free(VmaAllocation allocation)
	{
		vmaFreeMemory(s_Data->Allocator, allocation);
	}

	void ResourceAllocator::UnmapMemory(VmaAllocation allocation)
	{
		vmaUnmapMemory(s_Data->Allocator, allocation);
	}

	GPUMemoryStats ResourceAllocator::GetStats()
	{
		VmaStats stats;
		vmaCalculateStats(s_Data->Allocator, &stats);

		uint64_t usedMemory = stats.total.usedBytes;
		uint64_t freeMemory = stats.total.unusedBytes;

		return { usedMemory, freeMemory };
	}

	void ResourceAllocator::Init(Ref<Device> device)
	{
		s_Data = new ResourceAllocatorData();

		// Initialize VulkanMemoryAllocator
		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
		allocatorInfo.physicalDevice = device->GetPhysicalDevice()->GetVulkanPhysicalDevice();
		allocatorInfo.device = device->GetVulkanDevice();
		allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
		allocatorInfo.instance = RendererContext::GetInstance();

		vmaCreateAllocator(&allocatorInfo, &s_Data->Allocator);
	}

	VmaAllocator& ResourceAllocator::GetVMAAllocator()
	{
		return s_Data->Allocator;
	}

	void ResourceAllocator::Shutdown()
	{
		vmaDestroyAllocator(s_Data->Allocator);

		delete s_Data;
		s_Data = nullptr;
	}

	ResourceAllocator::ResourceAllocator(const std::string& tag)
		: m_Tag(tag)
	{
	}

	ResourceAllocator::~ResourceAllocator()
	{
	}

	VmaAllocation ResourceAllocator::AllocateBuffer(VkBufferCreateInfo bufferCreateInfo, VmaMemoryUsage usage, VkBuffer& outBuffer)
	{
		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = usage;

		VmaAllocation allocation;
		vmaCreateBuffer(s_Data->Allocator, &bufferCreateInfo, &allocCreateInfo, &outBuffer, &allocation, nullptr);
		
		VmaAllocationInfo allocInfo{};
		vmaGetAllocationInfo(s_Data->Allocator, allocation, &allocInfo);
		ALLOCATOR_LOG("ResourceAllocator ({0}): allocating buffer; size = {1}", m_Tag, utils::BytesToString(allocInfo.size));
		{
			s_Data->TotalAllocatedBytes += allocInfo.size;
			ALLOCATOR_LOG("ResourceAllocator ({0}): total allocated since start is {1}", m_Tag, utils::BytesToString(s_Data->TotalAllocatedBytes));
		}

		return allocation;
	
	}

	VmaAllocation ResourceAllocator::AllocateBuffer(VkBufferCreateInfo bufferCreateInfo, VmaMemoryUsage usage, VmaAllocationCreateFlags flags, VkBuffer& outBuffer)
	{

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = usage;
		//allocCreateInfo.flags = flags;

		VmaAllocation allocation;
		vmaCreateBuffer(s_Data->Allocator, &bufferCreateInfo, &allocCreateInfo, &outBuffer, &allocation, nullptr);

		VmaAllocationInfo allocInfo{};
		vmaGetAllocationInfo(s_Data->Allocator, allocation, &allocInfo);
		ALLOCATOR_LOG("ResourceAllocator ({0}): allocating buffer; size = {1}", m_Tag, utils::BytesToString(allocInfo.size));
		{
			s_Data->TotalAllocatedBytes += allocInfo.size;
			ALLOCATOR_LOG("ResourceAllocator ({0}): total allocated since start is {1}", m_Tag, utils::BytesToString(s_Data->TotalAllocatedBytes));
		}

		return allocation;
	}

	VmaAllocation ResourceAllocator::AllocateImage(VkImageCreateInfo imageCreateInfo, VmaMemoryUsage usage, VkImage& outImage)
	{
		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = usage;

		VmaAllocation allocation;
		vmaCreateImage(s_Data->Allocator, &imageCreateInfo, &allocCreateInfo, &outImage, &allocation, nullptr);

		VmaAllocationInfo allocInfo{};
		vmaGetAllocationInfo(s_Data->Allocator, allocation, &allocInfo);
		ALLOCATOR_LOG("ResourceAllocator ({0}): allocating buffer; size = {1}", m_Tag, utils::BytesToString(allocInfo.size));
		{
			s_Data->TotalAllocatedBytes += allocInfo.size;
			ALLOCATOR_LOG("ResourceAllocator ({0}): total allocated since start is {1}", m_Tag, utils::BytesToString(s_Data->TotalAllocatedBytes));
		}

		return allocation;
	}

	void ResourceAllocator::DestroyBuffer(VkBuffer buffer, VmaAllocation allocation)
	{
		CORE_ASSERT(buffer, "Null buffer");
		CORE_ASSERT(allocation, "Null memory allocation");
		vmaDestroyBuffer(s_Data->Allocator, buffer, allocation);
	}

	void ResourceAllocator::DestroyImage(VkImage image, VmaAllocation allocation)
	{
		CORE_ASSERT(image, "Null buffer");
		CORE_ASSERT(allocation, "Null memory allocation");
		vmaDestroyImage(s_Data->Allocator, image, allocation);
	}

}