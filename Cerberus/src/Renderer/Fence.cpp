#include "pch.h"
#include "Fence.h"
#include "Renderer/Renderer.h"
#include "Renderer/DeferredResourceDeletionQueue.h"
namespace cerberus
{
	Fence::Fence(uint32_t count, const std::string& debugName)
		: m_DebugName(debugName)
	{
		auto device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		m_Fences.resize(count);
		VkFenceCreateInfo fenceCreateInfo{};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		for (auto& fence : m_Fences)
			VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));
		
	}

	Fence::~Fence()
	{
		Release();
	}

	void Fence::Release()
	{
		if (!m_Fences.size())
			return;

		DeferredResourceDeletionQueue::SubmitCurrentFrame([fences = m_Fences]()
		{
			VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			for (auto& fence : fences)
				vkDestroyFence(device, fence, nullptr);
		});

		m_Fences.clear();
		
	}

	void Fence::Wait(uint32_t index, uint64_t timeout)
	{
		CORE_ASSERT(index < m_Fences.size(), "");
		auto device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		vkWaitForFences(device, 1, &m_Fences[index], VK_TRUE, timeout);
	}

	void Fence::Reset(uint32_t index)
	{
		CORE_ASSERT(index < m_Fences.size(), "");
		auto device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		vkResetFences(device, 1, &m_Fences[index]);
	}
}

