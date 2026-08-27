#pragma once
#include "Renderer/RHI/Command/CommandPool.h"
#include "Renderer/RHI/Command/CommandBufferView.h"

namespace cerberus
{
	struct PipelineStatistics
	{
		// Rasterization Pipeline (TODO: Geometry and Tesselation missing)
		uint64_t InputAssemblyVertices = 0;
		uint64_t InputAssemblyPrimitives = 0;
		uint64_t VertexShaderInvocations = 0;
		uint64_t ClippingInvocations = 0;
		uint64_t ClippingPrimitives = 0;
		uint64_t FragmentShaderInvocations = 0;
		// Compute Pipeline
		uint64_t ComputeShaderInvocations = 0;
	};

	class CommandBuffer
	{
	public:
		CommandBuffer(bool ownedBySwapChain = false, const std::string& debugName = "");
		CommandBuffer(CommandPool* commandPool, uint32_t count = 0, const std::string& debugName = "");
		CommandBuffer(uint32_t count = 0, const std::string& debugName = "");
		~CommandBuffer();

		// Queued API. These calls can be made from the application thread and are
		// executed later by RenderQueueManager on the render thread.
		void Begin();
		void End();
		void Submit();

		// Immediate API. Use these only when the caller is already executing on the
		// render thread / render queue.
		void BeginRenderThread();
		void EndRenderThread();
		void SubmitRenderThread();

		// Queued: safe to call from the application thread before Begin() is
		// queued. Their vkCmdWriteTimestamp is recorded later, by
		// RenderQueueManager, along with the rest of the queued recording.
		uint32_t BeginTimestampQuery();
		void EndTimestampQuery(uint32_t queryID);
		// Immediate: for a caller already executing on the render thread,
		// between BeginRenderThread() and EndRenderThread() (e.g.
		// RenderGraph::Execute). Calling the queued versions above from here
		// would defer their vkCmdWriteTimestamp past this recording's own
		// EndRenderThread()/SubmitRenderThread(), which is invalid.
		uint32_t BeginTimestampQueryRenderThread();
		void EndTimestampQueryRenderThread(uint32_t queryID);
		float GetExecutionGPUTime(uint32_t frameIndex, uint32_t queryIndex) const;
		float GetExecutionGPUTime(uint32_t queryIndex) const;

		PipelineStatistics GetPipelineStatistics() const;

		CommandBufferView GetRecordingView(uint32_t frameIndex) const
		{
			CORE_ASSERT(frameIndex < m_CommandBuffers.size(), "Overflow! Invalid command buffer index!");
			return CommandBufferView(m_CommandBuffers[frameIndex], frameIndex);
		}

		static Ref<CommandBuffer> CreateFromSwapchain();
		static Ref<CommandBuffer> Create(uint32_t count = 0,const std::string& debugName = "");
	private:
		void Invalidate(uint32_t count);

		void BeginInternal(uint32_t frameIndex, bool enableQueries);
		void EndInternal(uint32_t frameIndex, bool enableQueries);
		void SubmitInternal(uint32_t frameIndex, bool collectQueryResults);
	private:
		CommandPool* m_CommandPool = nullptr;
		std::vector<VkCommandBuffer> m_CommandBuffers;
		std::vector<VkFence> m_WaitFences;
		std::string m_DebugName;
		bool m_OwnedBySwapchain = false;
		// Statictis
		uint32_t m_PipelineQueryCount = 7;	// 7 pipeline stages
		std::vector<PipelineStatistics> m_PipelineStatisticsQueryResults;
		// Queries
		uint32_t m_TimestampQueryCount = 0;
		uint32_t m_TimestampNextAvailableQuery = 2;
		std::vector<VkQueryPool> m_TimestampQueryPools;
		std::vector<VkQueryPool> m_PipelineStatisticsQueryPools;
		std::vector<std::vector<uint64_t>> m_TimestampQueryResults;
		std::vector<std::vector<float>> m_ExecutionGPUTimes;
		std::vector<uint32_t> m_SubmittedTimestampQueryCounts;


	};
}
