#pragma once
#include "Renderer/ResourceAllocator.h"

namespace cerberus 
{
	enum class GPUBufferUsage
	{
		None = 0,
		IndexBufferUsage,
		VertexBufferUsage,
		ASBufferUsage,
		BLASBufferUsage,
		TLASBufferUsage,
		StagingUsage,
		UniformBufferUsage,
	};

	struct GPUBufferSpecification
	{
		GPUBufferUsage Usage;
		uint32_t Size;
		VmaMemoryUsage MemoryUsage;
		std::string DebugName;
	};

	struct GPUBuffer
	{
		VkBuffer Buffer;
		uint32_t Size;
		VmaAllocation Allocation;
		std::string DebugName = "None";

		GPUBuffer(const GPUBufferSpecification& bufferSpec);
		~GPUBuffer();
	};


}