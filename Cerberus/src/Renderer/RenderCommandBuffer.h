#pragma once
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

	class RenderCommandBuffer
	{
	public:
		RenderCommandBuffer(bool ownedBySwapChain = false, const std::string& debugName = "");
		RenderCommandBuffer(uint32_t count = 0, const std::string& debugName = "");
		~RenderCommandBuffer();

		// Start recording commands to command buffer
		void Begin();
		// Stop recording commands to command buffer
		void End();
		// Submit commands
		void Submit();

		uint32_t BeginTimestampQuery();
		void EndTimestampQuery(uint32_t queryID);
		float GetExecutionGPUTime(uint32_t frameIndex, uint32_t queryIndex) const;
		float GetExecutionGPUTime(uint32_t queryIndex) const;

		PipelineStatistics GetPipelineStatistics() const;

		void RT_Begin();
		void RT_End();
		void RT_Submit();

		VkCommandBuffer GetCommandBuffer(uint32_t frameIndex) const
		{
			CORE_ASSERT(frameIndex < m_CommandBuffers.size(), "Overflow! Invalid command buffer index!");
			return m_CommandBuffers[frameIndex];
		}

		VkCommandBuffer GetCommandBuffer() const;

		static Ref<RenderCommandBuffer> CreateFromSwapchain();
		static Ref<RenderCommandBuffer> Create(uint32_t count = 0,const std::string& debugName = "");
	private:
		void Invalidate(uint32_t count);
	private:
		VkCommandPool m_CommandPool = nullptr;
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


	};
}