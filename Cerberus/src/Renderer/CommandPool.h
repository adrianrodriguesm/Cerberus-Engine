#pragma once

namespace cerberus
{
	enum class CommandPoolQueueType
	{
		Graphics = 0,
		Compute
	};

	// Renderer-owned wrapper around VkCommandPool.
	//
	// CommandPool is command execution infrastructure, not a ResourceManager
	// resource. Persistent CommandBuffer wrappers and TransientCommandBuffer
	// one-shot helpers allocate from this object, keeping raw VkCommandPool usage
	// contained inside the renderer command layer.
	class CommandPool
	{
	public:
		CommandPool(CommandPoolQueueType queueType,
			VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			const std::string& debugName = "");
		~CommandPool();

		CommandPool(const CommandPool&) = delete;
		CommandPool& operator=(const CommandPool&) = delete;

		VkCommandBuffer AllocateCommandBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const;
		std::vector<VkCommandBuffer> AllocateCommandBuffers(uint32_t count, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const;

		void FreeCommandBuffer(VkCommandBuffer commandBuffer) const;
		void FreeCommandBuffers(const std::vector<VkCommandBuffer>& commandBuffers) const;
		void FreeCommandBuffers(uint32_t count, const VkCommandBuffer* commandBuffers) const;

		VkCommandPool GetVulkanCommandPool() const { return m_CommandPool; }
		VkQueue GetQueue() const;
		CommandPoolQueueType GetQueueType() const { return m_QueueType; }
		const std::string& GetDebugName() const { return m_DebugName; }

	private:
		CommandPoolQueueType m_QueueType = CommandPoolQueueType::Graphics;
		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
		std::string m_DebugName;
	};
}
