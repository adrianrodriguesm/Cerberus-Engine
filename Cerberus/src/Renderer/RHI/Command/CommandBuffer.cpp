#include "pch.h"
#include "CommandBuffer.h"
#include "Renderer/Core/Renderer.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Renderer/Memory/DeferredResourceDeletionQueue.h"
#include "Core/Profiler.h"
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
			m_CommandPool = Renderer::GetGraphicsCommandPool();
			CommandBuffer* instance = this;
			RenderQueueManager::Submit([instance]()
			{
				uint32_t framesInFlight = Renderer::GetRendererConfig().FramesInFlight;
				instance->Invalidate(framesInFlight);
			});
		}
	}

	CommandBuffer::CommandBuffer(CommandPool* commandPool, uint32_t count, const std::string& debugName)
		: m_CommandPool(commandPool), m_DebugName(debugName)
	{
		CORE_ASSERT(m_CommandPool, "CommandBuffer requires a valid CommandPool");

		CommandBuffer* instance = this;
		RenderQueueManager::Submit([instance, count]() mutable
		{
			if (count == 0)
				count = Renderer::GetRendererConfig().FramesInFlight;
			instance->Invalidate(count);
		});
	}

	CommandBuffer::CommandBuffer(uint32_t count, const std::string& debugName)
		: CommandBuffer(Renderer::GetGraphicsCommandPool(), count, debugName)
	{
	}

	CommandBuffer::~CommandBuffer()
	{
		if (m_OwnedBySwapchain)
			return;

		const VkCommandPool commandPool = m_CommandPool
			? m_CommandPool->GetVulkanCommandPool()
			: VK_NULL_HANDLE;
		DeferredResourceDeletionQueue::SubmitCurrentFrame([commandPool, commandBuffers = m_CommandBuffers, waitFences = m_WaitFences, timestampQueryPools = m_TimestampQueryPools, pipelineQueryPool = m_PipelineStatisticsQueryPools]()
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

			for (auto& timestampQueryPool : timestampQueryPools)
				vkDestroyQueryPool(vulkanDevice, timestampQueryPool, nullptr);
			
			for (auto& pipelineQueryPool : pipelineQueryPool)
				vkDestroyQueryPool(vulkanDevice, pipelineQueryPool, nullptr);

			for (auto& fence : waitFences)
				vkDestroyFence(vulkanDevice, fence, nullptr);

			if (commandPool != VK_NULL_HANDLE && !commandBuffers.empty())
			{
				vkFreeCommandBuffers(
					vulkanDevice,
					commandPool,
					static_cast<uint32_t>(commandBuffers.size()),
					commandBuffers.data());
			}
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
		// Reset timestamp cursor on the calling thread before the command is queued.
		m_TimestampNextAvailableQuery = 2;

		CommandBuffer* instance = this;
		RenderQueueManager::Submit([instance]()
		{
			const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
			instance->BeginInternal(frameIndex, true);
		});
	}

	void CommandBuffer::End()
	{
		CommandBuffer* instance = this;
		RenderQueueManager::Submit([instance]()
		{
			const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
			instance->EndInternal(frameIndex, true);
		});
	}

	void CommandBuffer::Submit()
	{
		if (m_OwnedBySwapchain)
			return;

		CommandBuffer* instance = this;
		RenderQueueManager::Submit([instance]() mutable
		{
			const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
			instance->SubmitInternal(frameIndex, true);
		});
	}

	void CommandBuffer::BeginRenderThread()
	{
		// Reset the timestamp cursor here too -- Begin() does this for the
		// queued path (see its comment), and this is the immediate path's
		// equivalent point: the start of a fresh recording for this frame's
		// command-buffer slot.
		m_TimestampNextAvailableQuery = 2;

		// Gated on Profiler::IsEnabled() so the switch actually removes every
		// query-related Vulkan call this frame (pool reset, the whole-frame
		// timestamp pair, pipeline-statistics begin/end, and the readback in
		// SubmitRenderThread below) -- not just the per-pass ones RenderGraph
		// separately gates in Execute().
		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		BeginInternal(frameIndex, Profiler::IsEnabled());
	}

	void CommandBuffer::EndRenderThread()
	{
		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		EndInternal(frameIndex, Profiler::IsEnabled());
	}

	void CommandBuffer::SubmitRenderThread()
	{
		if (m_OwnedBySwapchain)
			return;

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		SubmitInternal(frameIndex, Profiler::IsEnabled());
	}

	uint32_t CommandBuffer::BeginTimestampQuery()
	{
		CORE_ASSERT(m_TimestampNextAvailableQuery + 1 < m_TimestampQueryCount,
			"CommandBuffer timestamp query pool exhausted; raise maxUserQueries in Invalidate()");
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

	uint32_t CommandBuffer::BeginTimestampQueryRenderThread()
	{
		CORE_ASSERT(m_TimestampNextAvailableQuery + 1 < m_TimestampQueryCount,
			"CommandBuffer timestamp query pool exhausted; raise maxUserQueries in Invalidate()");
		const uint32_t queryIndex = m_TimestampNextAvailableQuery;
		m_TimestampNextAvailableQuery += 2;
		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		vkCmdWriteTimestamp(m_CommandBuffers[frameIndex], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_TimestampQueryPools[frameIndex], queryIndex);
		return queryIndex;
	}

	void CommandBuffer::EndTimestampQueryRenderThread(uint32_t queryID)
	{
		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		vkCmdWriteTimestamp(m_CommandBuffers[frameIndex], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampQueryPools[frameIndex], queryID + 1);
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
	void CommandBuffer::BeginInternal(uint32_t frameIndex, bool enableQueries)
	{
		CORE_ASSERT(frameIndex < m_CommandBuffers.size(), "Overflow! Invalid command buffer index!");

		VkCommandBuffer commandBuffer = m_CommandBuffers[frameIndex];

		VkCommandBufferBeginInfo bufferBeginInfo = {};
		bufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		bufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		bufferBeginInfo.pNext = nullptr;
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &bufferBeginInfo));

		if (!enableQueries)
			return;

		vkCmdResetQueryPool(commandBuffer, m_TimestampQueryPools[frameIndex], 0, m_TimestampQueryCount);
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_TimestampQueryPools[frameIndex], 0);

		vkCmdResetQueryPool(commandBuffer, m_PipelineStatisticsQueryPools[frameIndex], 0, m_PipelineQueryCount);
		vkCmdBeginQuery(commandBuffer, m_PipelineStatisticsQueryPools[frameIndex], 0, 0);
	}

	void CommandBuffer::EndInternal(uint32_t frameIndex, bool enableQueries)
	{
		CORE_ASSERT(frameIndex < m_CommandBuffers.size(), "Overflow! Invalid command buffer index!");

		VkCommandBuffer commandBuffer = m_CommandBuffers[frameIndex];

		if (enableQueries)
		{
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampQueryPools[frameIndex], 1);
			vkCmdEndQuery(commandBuffer, m_PipelineStatisticsQueryPools[frameIndex], 0);
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
	}

	void CommandBuffer::SubmitInternal(uint32_t frameIndex, bool collectQueryResults)
	{
		CORE_ASSERT(frameIndex < m_CommandBuffers.size(), "Overflow! Invalid command buffer index!");

		Ref<Device> device = RendererContext::GetCurrentDevice();
		VkDevice vulkanDevice = device->GetVulkanDevice();

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		VkCommandBuffer commandBuffer = m_CommandBuffers[frameIndex];
		submitInfo.pCommandBuffers = &commandBuffer;

		{
			// This is the CPU stalling until the GPU finishes this
			// frame-in-flight slot's *previous* use -- isolated in its own
			// scope so a large Render()/FrameSubmit total can be attributed
			// to an actual GPU-completion wait rather than guessed at.
			CORE_PROFILE_SCOPE("WaitForFence");
			VK_CHECK_RESULT(vkWaitForFences(vulkanDevice, 1, &m_WaitFences[frameIndex], VK_TRUE, UINT64_MAX));
		}
		VK_CHECK_RESULT(vkResetFences(vulkanDevice, 1, &m_WaitFences[frameIndex]));
		VK_CHECK_RESULT(vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, m_WaitFences[frameIndex]));

		if (!collectQueryResults)
			return;

		vkGetQueryPoolResults(device->GetVulkanDevice(), m_TimestampQueryPools[frameIndex], 0, m_TimestampNextAvailableQuery,
			m_TimestampNextAvailableQuery * sizeof(uint64_t), m_TimestampQueryResults[frameIndex].data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

		for (uint32_t i = 0; i < m_TimestampNextAvailableQuery; i += 2)
		{
			uint64_t startTime = m_TimestampQueryResults[frameIndex][i];
			uint64_t endTime = m_TimestampQueryResults[frameIndex][i + 1];
			float nsTime = endTime > startTime ? (endTime - startTime) * Renderer::GetDeviceLimits().timestampPeriod : 0.0f;
			m_ExecutionGPUTimes[frameIndex][i / 2] = nsTime * 0.000001f;
		}

		vkGetQueryPoolResults(device->GetVulkanDevice(), m_PipelineStatisticsQueryPools[frameIndex], 0, 1,
			sizeof(PipelineStatistics), &m_PipelineStatisticsQueryResults[frameIndex], sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
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
		CORE_ASSERT(m_CommandPool, "CommandBuffer requires a valid renderer-owned CommandPool");

		///////////////////////////////////////////////////////
		//// Allocate Command Buffers
		///////////////////////////////////////////////////////
		m_CommandBuffers = m_CommandPool->AllocateCommandBuffers(count, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
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
		// 16 covered RenderGraph's own ~8 passes; raised so general-purpose CPU/GPU
		// instrumentation (Core/Profiler.h) has real headroom beyond that.
		const uint32_t maxUserQueries = 64;
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
