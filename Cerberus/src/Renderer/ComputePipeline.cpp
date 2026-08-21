#include "pch.h"
#include "ComputePipeline.h"
#include "Renderer/DeferredResourceDeletionQueue.h"
#include "Renderer/RendererContext.h"
namespace cerberus
{
	ComputePipeline::ComputePipeline(Ref<Shader> computeShader)
		: m_Shader(std::move(computeShader))
	{
	}

	ComputePipeline::~ComputePipeline()
	{
		if (m_ComputePipeline == VK_NULL_HANDLE &&
			m_ComputePipelineLayout == VK_NULL_HANDLE &&
			m_PipelineCache == VK_NULL_HANDLE)
			return;

		DeferredResourceDeletionQueue::SubmitCurrentFrame([computePipeline = m_ComputePipeline, pipelineLayout = m_ComputePipelineLayout, pipelineCache = m_PipelineCache]()
		{
			VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			if (computePipeline != VK_NULL_HANDLE)
				vkDestroyPipeline(device, computePipeline, nullptr);
			if (pipelineCache != VK_NULL_HANDLE)
				vkDestroyPipelineCache(device, pipelineCache, nullptr);
			if (pipelineLayout != VK_NULL_HANDLE)
				vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		});
		m_ComputePipeline = VK_NULL_HANDLE;
		m_ComputePipelineLayout = VK_NULL_HANDLE;
		m_PipelineCache = VK_NULL_HANDLE;
	}

	void ComputePipeline::Invalidate()
	{
		CORE_ASSERT(m_Shader, "ComputePipeline has no shader");
		CORE_ASSERT(
			m_ComputePipeline == VK_NULL_HANDLE &&
			m_ComputePipelineLayout == VK_NULL_HANDLE &&
			m_PipelineCache == VK_NULL_HANDLE,
			"ComputePipeline is already initialized");

		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages = m_Shader->GetPipelineShaderModules();
		CORE_ASSERT(
			shaderStages.size() == 1 && shaderStages.front().stage == VK_SHADER_STAGE_COMPUTE_BIT,
			"ComputePipeline requires exactly one compute shader stage");

		std::vector<VkDescriptorSetLayout> descriptorSetLayouts = m_Shader->GetDescriptorSetLayouts();
		/////////////////////////////////
		///// Pipeline Layout
		/////////////////////////////////
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
		pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
		pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();
		std::vector<VkPushConstantRange> pushConstantRanges = m_Shader->GetVulkanPushConstantRanges();
		if (pushConstantRanges.size())
		{
			pipelineLayoutCreateInfo.pushConstantRangeCount = (uint32_t)pushConstantRanges.size();
			pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
		}
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &m_ComputePipelineLayout));
		/////////////////////////////////
		///// Compute Pipeline
		/////////////////////////////////
		VkComputePipelineCreateInfo computePipelineCreateInfo{};
		computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		computePipelineCreateInfo.layout = m_ComputePipelineLayout;
		computePipelineCreateInfo.flags = 0;
		computePipelineCreateInfo.stage = shaderStages.front();

		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &m_PipelineCache));
		VK_CHECK_RESULT(vkCreateComputePipelines(device, m_PipelineCache, 1, &computePipelineCreateInfo, nullptr, &m_ComputePipeline));
	}

}
