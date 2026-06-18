#include "pch.h"
#include "CommandBuffer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/DeferredResourceDeletionQueue.h"
#include "Core/Window.h"

namespace cerberus
{
	CommandBuffer::CommandBuffer(bool ownedBySwapChain, const std::string& debugName)
		: m_OwnedBySwapchain(ownedBySwapChain), m_DebugName(debugName)
	{
		if (m_OwnedBySwapchain)
		{
			Ref<Swapchain> swapchain = Window::GetCurrentSwapchain();
			uint32_t frames = (uint32_t)swapchain->GetSwapchainImages().size();
			m_CommandBuffers.resize(frames);
			for (uint32_t frame = 0; frame < frames; frame++)
				m_CommandBuffers[frame] = swapchain->GetDrawCommandBuffer(frame);
		}
		else
		{
			CommandBuffer* instance = this;
			RenderQueueManager::Submit([instance]()
			{
				uint32_t framesInFlight = Renderer::GetRendererConfig().FramesInFlight;
				instance->Invalidate(framesInFlight);
			});
		}
	}
	CommandBuffer::CommandBuffer(uint32_t count, const std::string& debugName)
		: m_DebugName(debugName)
	{
		CommandBuffer* instance = this;
		RenderQueueManager::Submit([instance, count]() mutable
		{
			if (count == 0)
				count = Renderer::GetRendererConfig().FramesInFlight;
			instance->Invalidate(count);
		});
	}
	CommandBuffer::~CommandBuffer()
	{
		if (m_OwnedBySwapchain)
			return;

		DeferredResourceDeletionQueue::SubmitCurrentFrame([commandPool = m_CommandPool, waitFences = m_WaitFences, timestampQueryPools = m_TimestampQueryPools, pipelineQueryPool = m_PipelineStatisticsQueryPools]()
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

			for (auto& timestampQueryPool : timestampQueryPools)
				vkDestroyQueryPool(vulkanDevice, timestampQueryPool, nullptr);
			
			for (auto& pipelineQueryPool : pipelineQueryPool)
				vkDestroyQueryPool(vulkanDevice, pipelineQueryPool, nullptr);

			for (auto& fence : waitFences)
				vkDestroyFence(vulkanDevice, fence, nullptr);
			
			vkDestroyCommandPool(vulkanDevice, commandPool, nullptr);
		});

		m_WaitFences.clear();
		m_CommandBuffers.clear();
		m_PipelineStatisticsQueryPools.clear();
		m_TimestampQueryPools.clear();
		m_TimestampQueryResults.clear();
		m_ExecutionGPUTimes.clear();
	}

	void CommandBuffer::Begin()
	{
		// Reset Timestamp of the next available query
		m_TimestampNextAvailableQuery = 2;
		CommandBuffer* instance = this;
		RenderQueueManager::Submit([instance]()
		{
			uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
			VkCommandBuffer commandBuffer = instance->m_CommandBuffers[frameIndex];
			// Information about how to begin each command buffer
			VkCommandBufferBeginInfo bufferBeginInfo = {};
			bufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			bufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;	// Buffer can be resubmitted when it has already been submitted and is awaiting execution
			bufferBeginInfo.pNext = nullptr;
			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &bufferBeginInfo));
		
			// Timestamp query
			vkCmdResetQueryPool(commandBuffer, instance->m_TimestampQueryPools[frameIndex], 0, instance->m_TimestampQueryCount);
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, instance->m_TimestampQueryPools[frameIndex], 0);

			// Pipeline stats query
			vkCmdResetQueryPool(commandBuffer, instance->m_PipelineStatisticsQueryPools[frameIndex], 0, instance->m_PipelineQueryCount);
			vkCmdBeginQuery(commandBuffer, instance->m_PipelineStatisticsQueryPools[frameIndex], 0, 0);

			
		});

	}

	void CommandBuffer::End()
	{
		CommandBuffer* instance = this;
		RenderQueueManager::Submit([instance]()
		{
			uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
			VkCommandBuffer commandBuffer = instance->m_CommandBuffers[frameIndex];
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, instance->m_TimestampQueryPools[frameIndex], 1);
			vkCmdEndQuery(commandBuffer, instance->m_PipelineStatisticsQueryPools[frameIndex], 0);
			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
		});
		
	}
	void CommandBuffer::Submit()
	{
		if (m_OwnedBySwapchain)
			return;

		CommandBuffer* instance = this;
		RenderQueueManager::Submit([instance]() mutable
		{
			Ref<Device> device = RendererContext::GetCurrentDevice();
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			VkCommandBuffer commandBuffer = instance->m_CommandBuffers[frameIndex];
			submitInfo.pCommandBuffers = &commandBuffer;

			// Make sure the frame we're requesting has finished rendering
			VK_CHECK_RESULT(vkWaitForFences(vulkanDevice, 1, &instance->m_WaitFences[frameIndex], VK_TRUE, UINT64_MAX));
			// Manually reset (close) fences
			VK_CHECK_RESULT(vkResetFences(vulkanDevice, 1, &instance->m_WaitFences[frameIndex]));
			// Submit command buffer to queue
			VK_CHECK_RESULT(vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, instance->m_WaitFences[frameIndex]));
		
			
			// Retrieve timestamp query results
			vkGetQueryPoolResults(device->GetVulkanDevice(), instance->m_TimestampQueryPools[frameIndex], 0, instance->m_TimestampNextAvailableQuery,
				instance->m_TimestampNextAvailableQuery * sizeof(uint64_t), instance->m_TimestampQueryResults[frameIndex].data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

			for (uint32_t i = 0; i < instance->m_TimestampNextAvailableQuery; i += 2)
			{
				uint64_t startTime = instance->m_TimestampQueryResults[frameIndex][i];
				uint64_t endTime = instance->m_TimestampQueryResults[frameIndex][i + 1];
				float nsTime = endTime > startTime ? (endTime - startTime) * Renderer::GetDeviceLimits().timestampPeriod : 0.0f;
				instance->m_ExecutionGPUTimes[frameIndex][i / 2] = nsTime * 0.000001f; // Time in ms
			}

			// Retrieve pipeline stats results
			vkGetQueryPoolResults(device->GetVulkanDevice(), instance->m_PipelineStatisticsQueryPools[frameIndex], 0, 1,
				sizeof(PipelineStatistics), &instance->m_PipelineStatisticsQueryResults[frameIndex], sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
			
		});

	}
	uint32_t CommandBuffer::BeginTimestampQuery()
	{
		uint32_t queryIndex = m_TimestampNextAvailableQuery;
		m_TimestampNextAvailableQuery += 2;
		CommandBuffer* instance = this;
		RenderQueueManager::Submit([instance, queryIndex]()
		{
			uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
			VkCommandBuffer commandBuffer = instance->m_CommandBuffers[frameIndex];
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, instance->m_TimestampQueryPools[frameIndex], queryIndex);
		});
		return queryIndex;
	}
	void CommandBuffer::EndTimestampQuery(uint32_t queryID)
	{
		CommandBuffer* instance = this;
		RenderQueueManager::Submit([instance, queryID]()
		{
			uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
			VkCommandBuffer commandBuffer = instance->m_CommandBuffers[frameIndex];
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, instance->m_TimestampQueryPools[frameIndex], queryID + 1);
		});
	}
	float CommandBuffer::GetExecutionGPUTime(uint32_t frameIndex, uint32_t queryIndex) const
	{
		if (queryIndex / 2 >= m_TimestampNextAvailableQuery / 2)
			return 0.0f;

		return m_ExecutionGPUTimes[frameIndex][queryIndex / 2];
	}

	float CommandBuffer::GetExecutionGPUTime(uint32_t queryIndex) const
	{
		return GetExecutionGPUTime(Renderer::GetCurrentFrameIndex(), queryIndex);
	}
	PipelineStatistics CommandBuffer::GetPipelineStatistics() const
	{
		return m_PipelineStatisticsQueryResults[Renderer::GetCurrentFrameIndex()];
	}
	void CommandBuffer::RT_Begin()
	{
		// Information about how to begin each command buffer
		VkCommandBufferBeginInfo bufferBeginInfo = {};
		bufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		bufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;	// Buffer can be resubmitted when it has already been submitted and is awaiting execution
		bufferBeginInfo.pNext = nullptr;
		VK_CHECK_RESULT(vkBeginCommandBuffer(m_CommandBuffers[Renderer::GetCurrentFrameIndex()], &bufferBeginInfo));
	}
	void CommandBuffer::RT_End()
	{
		VK_CHECK_RESULT(vkEndCommandBuffer(m_CommandBuffers[Renderer::GetCurrentFrameIndex()]));
	}
	void CommandBuffer::RT_Submit()
	{
		if (m_OwnedBySwapchain)
			return;


		Ref<Device> device = RendererContext::GetCurrentDevice();
		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		VkCommandBuffer commandBuffer = m_CommandBuffers[frameIndex];
		submitInfo.pCommandBuffers = &commandBuffer;

		// Make sure the frame we're requesting has finished rendering
		VK_CHECK_RESULT(vkWaitForFences(vulkanDevice, 1, &m_WaitFences[frameIndex], VK_TRUE, UINT64_MAX));
		// Manually reset (close) fences
		VK_CHECK_RESULT(vkResetFences(vulkanDevice, 1, &m_WaitFences[frameIndex]));
		// Submit command buffer to queue
		VK_CHECK_RESULT(vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, m_WaitFences[frameIndex]));
	}
	VkCommandBuffer CommandBuffer::GetCommandBuffer() const
	{
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		CORE_ASSERT(frameIndex < m_CommandBuffers.size(), "Overflow! Invalid command buffer index!");
		return m_CommandBuffers[frameIndex];
		
	}
	Ref<CommandBuffer> CommandBuffer::CreateFromSwapchain()
	{
		return CreateRef<CommandBuffer>(true, "Swapchain");
	}
	Ref<CommandBuffer> CommandBuffer::Create(uint32_t count, const std::string& debugName)
	{
		return CreateRef<CommandBuffer>(count, debugName);
	}
	void CommandBuffer::Invalidate(uint32_t count)
	{
		Ref<Device> device = RendererContext::GetCurrentDevice();
		///////////////////////////////////////////////////////
		//// Create Command Pool
		///////////////////////////////////////////////////////
		VkCommandPoolCreateInfo cmdPoolInfo = {};
		cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmdPoolInfo.queueFamilyIndex = device->GetPhysicalDevice()->GetQueueFamilyIndices().Graphics;
		cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK_RESULT(vkCreateCommandPool(device->GetVulkanDevice(), &cmdPoolInfo, nullptr, &m_CommandPool));
		///////////////////////////////////////////////////////
		//// Allocate Command Buffers
		///////////////////////////////////////////////////////
		VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
		commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandBufferAllocateInfo.commandPool = m_CommandPool;
		commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandBufferAllocateInfo.commandBufferCount = count;
		m_CommandBuffers.resize(count);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device->GetVulkanDevice(), &commandBufferAllocateInfo, m_CommandBuffers.data()));
		///////////////////////////////////////////////////////
		//// Create Fences
		///////////////////////////////////////////////////////
		VkFenceCreateInfo fenceCreateInfo{};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		m_WaitFences.resize(count);
		for (auto& fence : m_WaitFences)
			VK_CHECK_RESULT(vkCreateFence(device->GetVulkanDevice(), &fenceCreateInfo, nullptr, &fence));
		///////////////////////////////////////////////////////
		//// Create Query Timestamp Pool
		///////////////////////////////////////////////////////
		const uint32_t maxUserQueries = 16;
		m_TimestampQueryCount = 2 + 2 * maxUserQueries;
		VkQueryPoolCreateInfo queryPoolCreateInfo = {};
		queryPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		queryPoolCreateInfo.queryCount = m_TimestampQueryCount;
		m_TimestampQueryPools.resize(count);

		m_TimestampQueryResults.resize(count);
		for (auto& timestampQueryResults : m_TimestampQueryResults)
			timestampQueryResults.resize(m_TimestampQueryCount);

		m_ExecutionGPUTimes.resize(count);
		for (auto& executionGPUTimes : m_ExecutionGPUTimes)
			executionGPUTimes.resize(m_TimestampQueryCount / 2);

		for (auto& timestampQueryPools : m_TimestampQueryPools)
			VK_CHECK_RESULT(vkCreateQueryPool(device->GetVulkanDevice(), &queryPoolCreateInfo, nullptr, &timestampQueryPools));
		///////////////////////////////////////////////////////
		//// Create Pipeline statistics queries
		///////////////////////////////////////////////////////
		// NOTE: If the pipelineStatisticsQueryfeature is not enabled, queries
		//		 of type VK_QUERY_TYPE_PIPELINE_STATISTICS cannot be created.
		queryPoolCreateInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
		queryPoolCreateInfo.queryCount = m_PipelineQueryCount;
		queryPoolCreateInfo.pipelineStatistics =
			VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
			VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
			VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
			VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
			VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
			VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
			VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;

		m_PipelineStatisticsQueryPools.resize(count);
		for (auto& pipelineStatisticsQueryPools : m_PipelineStatisticsQueryPools)
			VK_CHECK_RESULT(vkCreateQueryPool(device->GetVulkanDevice(), &queryPoolCreateInfo, nullptr, &pipelineStatisticsQueryPools));

		m_PipelineStatisticsQueryResults.resize(count);
	}
}