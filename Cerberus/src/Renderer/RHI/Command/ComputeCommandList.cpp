#include "pch.h"
#include "ComputeCommandList.h"

#include "Renderer/RHI/Pipeline/ComputePipeline.h"
#include "Renderer/RHI/ResourceManager.h"

namespace cerberus
{
	ComputeCommandList::ComputeCommandList(
		CommandBufferView commandBuffer,
		RendererFrameStatistics* statistics)
		: CommandListBase(commandBuffer, statistics)
	{
	}

	void ComputeCommandList::BindDescriptorSets(Handle<BindGroup> bindGroupHandle, uint32_t firstSet)
	{
		BindDescriptorSetInternal(
			VK_PIPELINE_BIND_POINT_COMPUTE,
			m_BoundPipelineLayout,
			bindGroupHandle,
			firstSet);
	}

	void ComputeCommandList::BindDescriptorSets(
		std::span<const Handle<BindGroup>> bindGroups,
		uint32_t firstSet)
	{
		BindDescriptorSetsInternal(
			VK_PIPELINE_BIND_POINT_COMPUTE,
			m_BoundPipelineLayout,
			bindGroups,
			firstSet);
	}

	void ComputeCommandList::BindPipeline(Handle<ComputePipeline> pipelineHandle)
	{
		ComputePipeline* pipeline = ResourceManager::GetComputePipeline(pipelineHandle);
		CORE_ASSERT(pipeline && pipeline->GetHandle() != VK_NULL_HANDLE, "Invalid compute pipeline handle");
		vkCmdBindPipeline(GetNative(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->GetHandle());
		m_BoundPipelineLayout = pipeline->GetVulkanPipelineLayout();
	}

	void ComputeCommandList::BindDescriptorSets(
		std::span<const VkDescriptorSet> descriptorSets,
		uint32_t firstSet)
	{
		BindDescriptorSetsInternal(
			VK_PIPELINE_BIND_POINT_COMPUTE,
			m_BoundPipelineLayout,
			descriptorSets,
			firstSet);
	}

	void ComputeCommandList::SetPushConstants(std::span<const std::byte> data, uint32_t offset)
	{
		CORE_ASSERT(m_BoundPipelineLayout != VK_NULL_HANDLE, "Bind a compute pipeline before push constants");
		PushConstants(m_BoundPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, data, offset);
	}

	void ComputeCommandList::Dispatch(
		uint32_t groupCountX,
		uint32_t groupCountY,
		uint32_t groupCountZ)
	{
		CORE_ASSERT(m_BoundPipelineLayout != VK_NULL_HANDLE, "Bind a compute pipeline before dispatch");
		vkCmdDispatch(GetNative(), groupCountX, groupCountY, groupCountZ);
		if (RendererFrameStatistics* statistics = GetStatistics())
			++statistics->ComputeDispatchCount;
	}
}
