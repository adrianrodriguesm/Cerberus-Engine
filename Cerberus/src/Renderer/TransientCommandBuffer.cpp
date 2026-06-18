#include "pch.h"
#include "TransientCommandBuffer.h"
#include "Renderer/RendererContext.h"

namespace cerberus
{
	TransientCommandBuffer::TransientCommandBuffer(VkCommandBuffer commandBuffer, VkCommandPool commandPool, VkQueue queue)
		: m_CommandBuffer(commandBuffer), m_CommandPool(commandPool), m_Queue(queue)
	{
	}

	TransientCommandBuffer::~TransientCommandBuffer()
	{
		Free();
	}

	TransientCommandBuffer::TransientCommandBuffer(TransientCommandBuffer&& other) noexcept
	{
		MoveFrom(std::move(other));
	}

	TransientCommandBuffer& TransientCommandBuffer::operator=(TransientCommandBuffer&& other) noexcept
	{
		if (this != &other)
		{
			Free();
			MoveFrom(std::move(other));
		}

		return *this;
	}

	void TransientCommandBuffer::SubmitAndWait(bool waitForQueueIdle)
	{
		CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "Cannot submit an invalid transient command buffer");
		CORE_ASSERT(m_CommandPool != VK_NULL_HANDLE, "Transient command buffer is missing its command pool");
		CORE_ASSERT(m_Queue != VK_NULL_HANDLE, "Transient command buffer is missing its queue");

		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		VK_CHECK_RESULT(vkEndCommandBuffer(m_CommandBuffer));

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_CommandBuffer;

		VkFence fence = VK_NULL_HANDLE;
		VkFenceCreateInfo fenceCreateInfo = {};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreateFence(vulkanDevice, &fenceCreateInfo, nullptr, &fence));

		VK_CHECK_RESULT(vkQueueSubmit(m_Queue, 1, &submitInfo, fence));
		VK_CHECK_RESULT(vkWaitForFences(vulkanDevice, 1, &fence, VK_TRUE, UINT64_MAX));

		if (waitForQueueIdle)
			VK_CHECK_RESULT(vkQueueWaitIdle(m_Queue));

		vkDestroyFence(vulkanDevice, fence, nullptr);

		Free();
	}

	VkCommandBuffer TransientCommandBuffer::Release()
	{
		VkCommandBuffer commandBuffer = m_CommandBuffer;
		m_CommandBuffer = VK_NULL_HANDLE;
		m_CommandPool = VK_NULL_HANDLE;
		m_Queue = VK_NULL_HANDLE;
		return commandBuffer;
	}

	void TransientCommandBuffer::Free()
	{
		if (m_CommandBuffer == VK_NULL_HANDLE)
			return;

		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		vkFreeCommandBuffers(vulkanDevice, m_CommandPool, 1, &m_CommandBuffer);

		m_CommandBuffer = VK_NULL_HANDLE;
		m_CommandPool = VK_NULL_HANDLE;
		m_Queue = VK_NULL_HANDLE;
	}

	void TransientCommandBuffer::MoveFrom(TransientCommandBuffer&& other) noexcept
	{
		m_CommandBuffer = other.m_CommandBuffer;
		m_CommandPool = other.m_CommandPool;
		m_Queue = other.m_Queue;

		other.m_CommandBuffer = VK_NULL_HANDLE;
		other.m_CommandPool = VK_NULL_HANDLE;
		other.m_Queue = VK_NULL_HANDLE;
	}
}
