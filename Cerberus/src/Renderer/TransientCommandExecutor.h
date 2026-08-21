#pragma once

#include "Renderer/CommandPool.h"
#include "Renderer/TransientCommandBuffer.h"

#include <vector>

namespace cerberus
{
	// Owns one-shot command allocation/submission policy. Returned command buffers
	// are move-only leases; recording views borrow their native handle.
	class TransientCommandExecutor final
	{
	public:
		explicit TransientCommandExecutor(CommandPool& immediateCommandPool);
		~TransientCommandExecutor();

		TransientCommandExecutor(const TransientCommandExecutor&) = delete;
		TransientCommandExecutor& operator=(const TransientCommandExecutor&) = delete;

		TransientCommandBuffer Begin();
		TransientCommandBuffer BeginGraphics() { return Begin(); }
		TransientCommandBuffer BeginCompute() { return Begin(); }

	private:
		friend class TransientCommandBuffer;

		void SubmitAndWait(TransientCommandBuffer& commandBuffer);
		void Recycle(TransientCommandBuffer& commandBuffer);

	private:
		CommandPool* m_ImmediateCommandPool = nullptr;
		VkFence m_SubmissionFence = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> m_AvailableCommandBuffers;
		uint32_t m_ActiveLeaseCount = 0;
	};
}
