#include "pch.h"
#include "TransientCommandExecutor.h"

#include "Renderer/Core/RendererContext.h"

namespace cerberus
{
	TransientCommandExecutor::TransientCommandExecutor(CommandPool& immediateCommandPool)
		: m_ImmediateCommandPool(&immediateCommandPool)
	{
		CORE_ASSERT(
			m_ImmediateCommandPool->GetQueueType() == CommandPoolQueueType::Graphics,
			"Synchronous transient work must use the graphics+compute queue before cross-queue ownership tracking exists");

		VkFenceCreateInfo fenceCreateInfo{};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &m_SubmissionFence));
	}

	TransientCommandExecutor::~TransientCommandExecutor()
	{
		CORE_ASSERT(m_ActiveLeaseCount == 0,
			"TransientCommandExecutor destroyed with active command-buffer leases");

		if (!m_AvailableCommandBuffers.empty())
			m_ImmediateCommandPool->FreeCommandBuffers(m_AvailableCommandBuffers);

		m_AvailableCommandBuffers.clear();

		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		if (m_SubmissionFence != VK_NULL_HANDLE)
			vkDestroyFence(device, m_SubmissionFence, nullptr);

		m_SubmissionFence = VK_NULL_HANDLE;
	}

	TransientCommandBuffer TransientCommandExecutor::Begin()
	{
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		if (m_AvailableCommandBuffers.empty())
		{
			commandBuffer = m_ImmediateCommandPool->AllocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		}
		else
		{
			commandBuffer = m_AvailableCommandBuffers.back();
			m_AvailableCommandBuffers.pop_back();
			VK_CHECK_RESULT(vkResetCommandBuffer(commandBuffer, 0));
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));
		++m_ActiveLeaseCount;

		return TransientCommandBuffer(commandBuffer, m_ImmediateCommandPool, this);
	}

	void TransientCommandExecutor::SubmitAndWait(TransientCommandBuffer& commandBuffer)
	{
		CORE_ASSERT(commandBuffer.m_CommandBuffer != VK_NULL_HANDLE, "Cannot submit an invalid transient command buffer");
		CORE_ASSERT(commandBuffer.m_CommandPool, "Transient command buffer has no command pool");
		CORE_ASSERT(commandBuffer.m_Executor == this, "Transient command buffer belongs to another executor");

		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		VkQueue queue = commandBuffer.m_CommandPool->GetQueue();

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer.m_CommandBuffer));
		VK_CHECK_RESULT(vkWaitForFences(device, 1, &m_SubmissionFence, VK_TRUE, UINT64_MAX));
		VK_CHECK_RESULT(vkResetFences(device, 1, &m_SubmissionFence));

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer.m_CommandBuffer;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, m_SubmissionFence));
		VK_CHECK_RESULT(vkWaitForFences(device, 1, &m_SubmissionFence, VK_TRUE, UINT64_MAX));

		Recycle(commandBuffer);
	}

	void TransientCommandExecutor::Recycle(TransientCommandBuffer& commandBuffer)
	{
		if (commandBuffer.m_CommandBuffer == VK_NULL_HANDLE)
			return;

		CORE_ASSERT(commandBuffer.m_CommandPool, "Transient command buffer has no command pool");
		CORE_ASSERT(commandBuffer.m_Executor == this, "Transient command buffer belongs to another executor");

		CORE_ASSERT(commandBuffer.m_CommandPool == m_ImmediateCommandPool, "Transient command buffer belongs to another command pool");
		CORE_ASSERT(m_ActiveLeaseCount > 0, "Transient command-buffer lease count underflow");

		m_AvailableCommandBuffers.push_back(commandBuffer.m_CommandBuffer);
		--m_ActiveLeaseCount;

		commandBuffer.m_CommandBuffer = VK_NULL_HANDLE;
		commandBuffer.m_CommandPool = nullptr;
		commandBuffer.m_Executor = nullptr;
	}

}
