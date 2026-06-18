#pragma once
#include "Renderer/CommandPool.h"

namespace cerberus
{
	// Lightweight wrapper for one-shot command buffers used by uploads, copies,
	// image transitions and other immediate setup work.
	//
	// Persistent frame command buffers are represented by CommandBuffer.
	// TransientCommandBuffer borrows a renderer-owned CommandPool and frees its
	// command buffer back to that pool after SubmitAndWait().
	class TransientCommandBuffer
	{
	public:
		TransientCommandBuffer() = default;
		TransientCommandBuffer(VkCommandBuffer commandBuffer, CommandPool* commandPool);
		~TransientCommandBuffer();

		TransientCommandBuffer(const TransientCommandBuffer&) = delete;
		TransientCommandBuffer& operator=(const TransientCommandBuffer&) = delete;

		TransientCommandBuffer(TransientCommandBuffer&& other) noexcept;
		TransientCommandBuffer& operator=(TransientCommandBuffer&& other) noexcept;

		VkCommandBuffer GetNative() const { return m_CommandBuffer; }
		operator VkCommandBuffer() const { return GetNative(); }
		bool IsValid() const { return m_CommandBuffer != VK_NULL_HANDLE; }

		// Ends the command buffer, submits it, waits for completion and frees it from
		// its owning command pool.
		void SubmitAndWait(bool waitForQueueIdle = false);

		// Legacy escape hatch used by Renderer::GetCommandBuffer(). Ownership is
		// transferred to the caller, who must later free the raw command buffer.
		VkCommandBuffer Release();

	private:
		void Free();
		void MoveFrom(TransientCommandBuffer&& other) noexcept;

	private:
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
		CommandPool* m_CommandPool = nullptr;
	};
}
