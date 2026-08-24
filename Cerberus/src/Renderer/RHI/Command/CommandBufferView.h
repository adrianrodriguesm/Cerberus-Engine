#pragma once

#include "Core/Base.h"

namespace cerberus
{
	class CommandBuffer;
	class CommandListBase;
	class TransientCommandBuffer;

	// Non-owning view of one already-begun command buffer recording.
	//
	// CommandBuffer and TransientCommandBuffer retain allocation, submission and
	// synchronization ownership. Typed command lists receive this view so they do
	// not need separate constructors for persistent and transient owners.
	class CommandBufferView final
	{
	public:
		uint32_t GetFrameIndex() const { return m_FrameIndex; }
		bool IsValid() const { return m_CommandBuffer != VK_NULL_HANDLE; }

	private:
		friend class CommandBuffer;
		friend class CommandListBase;
		friend class TransientCommandBuffer;

		VkCommandBuffer GetNative() const { return m_CommandBuffer; }

		CommandBufferView(VkCommandBuffer commandBuffer, uint32_t frameIndex)
			: m_CommandBuffer(commandBuffer), m_FrameIndex(frameIndex)
		{
		}

	private:
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
		uint32_t m_FrameIndex = 0;
	};
}
