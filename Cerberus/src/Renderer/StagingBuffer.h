#pragma once
#include "Renderer/GPUBuffer.h"
#include "Core/Handle.h"
namespace cerberus
{
	class StagingBuffer
	{
	public:
		StagingBuffer(uint32_t size, bool hostToDevice = true);
		~StagingBuffer();
		void CopyToBuffer(Handle<GPUBuffer> buffer, VkDeviceSize offset, VkDeviceSize size, const void* data);

	private:
		Handle<GPUBuffer> m_GPUBuffer;

	};
}
