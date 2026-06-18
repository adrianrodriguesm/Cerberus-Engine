#include "pch.h"
#include "CommandPool.h"
#include "Renderer/RendererContext.h"

namespace cerberus
{
	CommandPool::CommandPool(CommandPoolQueueType queueType, VkCommandPoolCreateFlags flags, const std::string& debugName)
		: m_QueueType(queueType), m_DebugName(debugName)
	{
		Ref<Device> device = RendererContext::GetCurrentDevice();
		CORE_ASSERT(device, "Cannot create CommandPool without a valid Renderer device");

		const auto& queueFamilyIndices = device->GetPhysicalDevice()->GetQueueFamilyIndices();
		const uint32_t queueFamilyIndex = (m_QueueType == CommandPoolQueueType::Compute)
			? queueFamilyIndices.Compute
			: queueFamilyIndices.Graphics;

		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndex;
		poolInfo.flags = flags;

		VK_CHECK_RESULT(vkCreateCommandPool(device->GetVulkanDevice(), &poolInfo, nullptr, &m_CommandPool));
	}

	CommandPool::~CommandPool()
	{
		if (m_CommandPool == VK_NULL_HANDLE)
			return;

		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		vkDestroyCommandPool(vulkanDevice, m_CommandPool, nullptr);
		m_CommandPool = VK_NULL_HANDLE;
	}

	VkCommandBuffer CommandPool::AllocateCommandBuffer(VkCommandBufferLevel level) const
	{
		std::vector<VkCommandBuffer> commandBuffers = AllocateCommandBuffers(1, level);
		return commandBuffers[0];
	}

	std::vector<VkCommandBuffer> CommandPool::AllocateCommandBuffers(uint32_t count, VkCommandBufferLevel level) const
	{
		CORE_ASSERT(m_CommandPool != VK_NULL_HANDLE, "Cannot allocate command buffers from a null CommandPool");
		CORE_ASSERT(count > 0, "Cannot allocate zero command buffers");

		VkCommandBufferAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = m_CommandPool;
		allocateInfo.level = level;
		allocateInfo.commandBufferCount = count;

		std::vector<VkCommandBuffer> commandBuffers(count);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(RendererContext::GetCurrentDevice()->GetVulkanDevice(), &allocateInfo, commandBuffers.data()));
		return commandBuffers;
	}

	void CommandPool::FreeCommandBuffer(VkCommandBuffer commandBuffer) const
	{
		if (commandBuffer == VK_NULL_HANDLE)
			return;

		FreeCommandBuffers(1, &commandBuffer);
	}

	void CommandPool::FreeCommandBuffers(const std::vector<VkCommandBuffer>& commandBuffers) const
	{
		if (commandBuffers.empty())
			return;

		FreeCommandBuffers(static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
	}

	void CommandPool::FreeCommandBuffers(uint32_t count, const VkCommandBuffer* commandBuffers) const
	{
		if (count == 0 || !commandBuffers)
			return;

		CORE_ASSERT(m_CommandPool != VK_NULL_HANDLE, "Cannot free command buffers from a null CommandPool");
		vkFreeCommandBuffers(RendererContext::GetCurrentDevice()->GetVulkanDevice(), m_CommandPool, count, commandBuffers);
	}

	VkQueue CommandPool::GetQueue() const
	{
		Ref<Device> device = RendererContext::GetCurrentDevice();
		return m_QueueType == CommandPoolQueueType::Compute
			? device->GetComputeQueue()
			: device->GetGraphicsQueue();
	}
}
