#pragma once
#include "Renderer/GPUBuffer.h"
namespace cerberus
{
	class ResourceManager final
	{
	public:
		static GPUBuffer CreateBuffer(const std::vector<T>& data, VkBufferUsageFlags usage, );
	};
}