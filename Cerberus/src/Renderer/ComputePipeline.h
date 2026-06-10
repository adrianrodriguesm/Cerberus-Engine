#pragma once

#include "Renderer/Shader.h"
#include "Renderer/CommandBuffer.h"
namespace cerberus
{
	class ComputePipeline
	{
	public:
		ComputePipeline(Ref<Shader> computeShader);
		~ComputePipeline();

		void Invalidate();

		void Begin(Ref<CommandBuffer> commandBuffer = nullptr);
		void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ, VkDescriptorSet descriptorSet, uint32_t firstSet = 0);
		void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ, const std::vector<VkDescriptorSet>& descriptorSets, uint32_t firstSet = 0);
		void End();
		void SetPushConstants(const void* data, uint32_t size);
		Ref<Shader> GetShader() { return m_Shader; }

		static void ReleaseComputeFence();
	private:
		Ref<Shader> m_Shader;
		VkPipelineLayout m_ComputePipelineLayout = nullptr;
		VkPipelineCache m_PipelineCache = nullptr;
		VkPipeline m_ComputePipeline = nullptr;

		VkCommandBuffer m_ActiveComputeCommandBuffer = nullptr;
		bool m_UsingGraphicsQueue = false;
	};

}

