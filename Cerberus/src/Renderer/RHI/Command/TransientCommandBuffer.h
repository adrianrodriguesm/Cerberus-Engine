#pragma once
#include "Renderer/RHI/Command/CommandPool.h"
#include "Renderer/RHI/Command/CommandBufferView.h"

namespace cerberus
{
	class TransientCommandExecutor;

	// Lightweight wrapper for one-shot command buffers used by uploads, copies,
	// image transitions and other immediate setup work.
	//
	// Persistent frame command buffers are represented by CommandBuffer.
	// TransientCommandBuffer borrows a renderer-owned CommandPool and returns its
	// native command buffer to TransientCommandExecutor's recycle cache.
	class TransientCommandBuffer
	{
	public:
		TransientCommandBuffer() = default;
		~TransientCommandBuffer();

		TransientCommandBuffer(const TransientCommandBuffer&) = delete;
		TransientCommandBuffer& operator=(const TransientCommandBuffer&) = delete;

		TransientCommandBuffer(TransientCommandBuffer&& other) noexcept;
		TransientCommandBuffer& operator=(TransientCommandBuffer&& other) noexcept;

		VkCommandBuffer GetNative() const { return m_CommandBuffer; }
		bool IsValid() const { return m_CommandBuffer != VK_NULL_HANDLE; }
		CommandBufferView GetRecordingView(uint32_t frameIndex = 0) const
		{
			return CommandBufferView(m_CommandBuffer, frameIndex);
		}

		// Ends the command buffer, submits it, waits for completion and recycles it.
		void SubmitAndWait();

	private:
		friend class TransientCommandExecutor;

		TransientCommandBuffer(
			VkCommandBuffer commandBuffer,
			CommandPool* commandPool,
			TransientCommandExecutor* executor);
		void Free();
		void MoveFrom(TransientCommandBuffer&& other) noexcept;

	private:
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
		CommandPool* m_CommandPool = nullptr;
		TransientCommandExecutor* m_Executor = nullptr;
	};
}
