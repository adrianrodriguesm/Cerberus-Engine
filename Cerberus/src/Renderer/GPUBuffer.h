#pragma once
#include "Renderer/ResourceAllocator.h"

namespace cerberus 
{
	struct GPUBuffer
	{
		VkBuffer Buffer;
		uint32_t Size;
		VmaAllocation Allocation;
	};
}