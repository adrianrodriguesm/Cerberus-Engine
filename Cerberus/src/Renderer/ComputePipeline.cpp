#include "pch.h"
#include "ComputePipeline.h"
#include "Renderer/Renderer.h"
#include "Core/Timer.h"
#include "Renderer/Fence.h"
#include "Renderer/RenderQueueManager.h"
namespace cerberus
{
	static Ref<Fence> s_ComputeFence;

	ComputePipeline::ComputePipeline(Ref<Shader> computeShader)
		: m_Shader(computeShader)
	{
		ComputePipeline* instance = this;
		RenderQueueManager::Submit([instance]()
		{
			if (!s_ComputeFence)
			{
				uint32_t framesInFlight = Renderer::GetRendererConfig().FramesInFlight;
				s_ComputeFence = CreateRef<Fence>(framesInFlight, "ComputeFence");
			}
			instance->Invalidate();
		});
	}

	ComputePipeline::~ComputePipeline()
	{
		if (!m_ComputePipeline)
			return;

		RenderQueueManager::SubmitResourceFree([computePipeline = m_ComputePipeline, pipelineLayout = m_ComputePipelineLayout, pipelineCache = m_PipelineCache]()
		{
			VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyPipelineCache(device, pipelineCache, nullptr);
			vkDestroyPipeline(device, computePipeline, nullptr);
		});
		m_ComputePipeline = nullptr;
		m_ComputePipelineLayout = nullptr;
		m_PipelineCache = nullptr;
	}

	void ComputePipeline::Invalidate()
	{
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
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
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages = m_Shader->GetPipelineShaderModules();
		computePipelineCreateInfo.stage = shaderStages[0];	// Only compute

		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &m_PipelineCache));
		VK_CHECK_RESULT(vkCreateComputePipelines(device, m_PipelineCache, 1, &computePipelineCreateInfo, nullptr, &m_ComputePipeline));
	}

	void ComputePipeline::Begin(Ref<CommandBuffer> commandBuffer)
	{
		CORE_ASSERT(!m_ActiveComputeCommandBuffer, "Command already created!");
		if (commandBuffer)
		{
			uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
			m_ActiveComputeCommandBuffer = commandBuffer->GetCommandBuffer(frameIndex);
			m_UsingGraphicsQueue = true;
		}
		else
		{
			m_ActiveComputeCommandBuffer = Renderer::GetCommandBuffer(true, true);
			m_UsingGraphicsQueue = false;
		}
		vkCmdBindPipeline(m_ActiveComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipeline);
	}

	void ComputePipeline::End()
	{
		CORE_ASSERT(m_ActiveComputeCommandBuffer, "Command Buffer missing!");
		Ref<Device> device = RendererContext::GetCurrentDevice();
		VkDevice vulkanDevice = device->GetVulkanDevice();
		if (!m_UsingGraphicsQueue)
		{
			VkQueue computeQueue = device->GetComputeQueue();

			vkEndCommandBuffer(m_ActiveComputeCommandBuffer);

			uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			s_ComputeFence->Wait(currentFrame, UINT64_MAX);
			// Manually close Fence
			s_ComputeFence->Reset(currentFrame);

			VkSubmitInfo computeSubmitInfo{};
			computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			computeSubmitInfo.commandBufferCount = 1;
			computeSubmitInfo.pCommandBuffers = &m_ActiveComputeCommandBuffer;
			VK_CHECK_RESULT(vkQueueSubmit(computeQueue, 1, &computeSubmitInfo, s_ComputeFence->GetVulkanFence(currentFrame)));

			// Wait for execution of compute shader to complete
			// Currently this is here for "safety"
			{
				Timer timer;
				s_ComputeFence->Wait(currentFrame, UINT64_MAX);
				NV_CORE_TRACE("Compute shader execution took {0} ms", timer.ElapsedMilliseconds());
			}
		}
		m_ActiveComputeCommandBuffer = nullptr;
	}

	void ComputePipeline::SetPushConstants(const void* data, uint32_t size)
	{
		CORE_ASSERT(m_ActiveComputeCommandBuffer, "Command Buffer missing");
		vkCmdPushConstants(m_ActiveComputeCommandBuffer, m_ComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, size, data);
	}

	void ComputePipeline::ReleaseComputeFence()
	{
		if(s_ComputeFence)
			s_ComputeFence->Release();
	}

	void ComputePipeline::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ, VkDescriptorSet descriptorSet, uint32_t firstSet)
	{
		CORE_ASSERT(m_ActiveComputeCommandBuffer, "Command Buffer missing!");

		vkCmdBindDescriptorSets(m_ActiveComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipelineLayout, 0, 1, &descriptorSet, 0, 0);
		vkCmdDispatch(m_ActiveComputeCommandBuffer, groupCountX, groupCountY, groupCountZ);
	}

	void ComputePipeline::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ, const std::vector<VkDescriptorSet>& descriptorSets, uint32_t firstSet)
	{
		CORE_ASSERT(m_ActiveComputeCommandBuffer, "Command Buffer missing!");
		vkCmdBindDescriptorSets(m_ActiveComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipelineLayout, firstSet, descriptorSets.size(), descriptorSets.data(), 0, 0);
		vkCmdDispatch(m_ActiveComputeCommandBuffer, groupCountX, groupCountY, groupCountZ);
	}


}
