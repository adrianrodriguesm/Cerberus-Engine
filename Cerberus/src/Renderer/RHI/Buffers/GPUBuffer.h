#pragma once
#include "Renderer/Memory/ResourceAllocator.h"

namespace cerberus 
{
	enum class BufferUsageType
	{
		Default = 0, Static = Default, Dynamic = 1
	};

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
		ShaderBindingTableUsage,
		StorageBufferUsage,
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
		GPUBuffer(GPUBuffer&& other) noexcept;
		GPUBuffer(const GPUBuffer&) = delete;
		GPUBuffer& operator=(const GPUBuffer&) = delete;
		GPUBuffer& operator=(GPUBuffer&&) = delete;
		~GPUBuffer();

		VkDeviceAddress GetDeviceAddress() const;
	};


}