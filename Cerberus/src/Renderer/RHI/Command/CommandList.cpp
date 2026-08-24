#include "pch.h"
#include "CommandList.h"

#include "Renderer/RHI/ResourceManager.h"

namespace cerberus
{
	CommandListBase::CommandListBase(
		CommandBufferView commandBuffer,
		RendererFrameStatistics* statistics)
		: m_CommandBuffer(commandBuffer), m_Statistics(statistics)
	{
		CORE_ASSERT(m_CommandBuffer.IsValid(), "CommandList requires a valid command buffer");
	}

	void CommandListBase::BindDescriptorSetInternal(
		VkPipelineBindPoint bindPoint,
		VkPipelineLayout pipelineLayout,
		Handle<BindGroup> bindGroupHandle,
		uint32_t firstSet) const
	{
		BindGroup* bindGroup = ResourceManager::GetBindGroup(bindGroupHandle);
		CORE_ASSERT(bindGroup, "Invalid BindGroup");

		const VkDescriptorSet descriptorSet = bindGroup->GetHandle(GetFrameIndex());
		CORE_ASSERT(descriptorSet != VK_NULL_HANDLE, "BindGroup has no descriptor set for this frame");
		BindDescriptorSetsInternal(
			bindPoint,
			pipelineLayout,
			std::span<const VkDescriptorSet>(&descriptorSet, 1),
			firstSet);
	}

	void CommandListBase::BindDescriptorSetsInternal(
		VkPipelineBindPoint bindPoint,
		VkPipelineLayout pipelineLayout,
		std::span<const Handle<BindGroup>> bindGroups,
		uint32_t firstSet) const
	{
		if (bindGroups.empty())
			return;

		if (bindGroups.size() == 1)
		{
			BindDescriptorSetInternal(bindPoint, pipelineLayout, bindGroups.front(), firstSet);
			return;
		}

		std::vector<VkDescriptorSet> descriptorSets;
		descriptorSets.reserve(bindGroups.size());
		for (Handle<BindGroup> bindGroupHandle : bindGroups)
		{
			BindGroup* bindGroup = ResourceManager::GetBindGroup(bindGroupHandle);
			CORE_ASSERT(bindGroup, "Invalid BindGroup");
			const VkDescriptorSet descriptorSet = bindGroup->GetHandle(GetFrameIndex());
			CORE_ASSERT(descriptorSet != VK_NULL_HANDLE, "BindGroup has no descriptor set for this frame");
			descriptorSets.push_back(descriptorSet);
		}

		BindDescriptorSetsInternal(bindPoint, pipelineLayout, descriptorSets, firstSet);
	}

	void CommandListBase::BindDescriptorSetsInternal(
		VkPipelineBindPoint bindPoint,
		VkPipelineLayout pipelineLayout,
		std::span<const VkDescriptorSet> descriptorSets,
		uint32_t firstSet) const
	{
		CORE_ASSERT(pipelineLayout != VK_NULL_HANDLE, "Bind a pipeline before descriptor sets");
		if (descriptorSets.empty())
			return;

		vkCmdBindDescriptorSets(
			GetNative(),
			bindPoint,
			pipelineLayout,
			firstSet,
			static_cast<uint32_t>(descriptorSets.size()),
			descriptorSets.data(),
			0,
			nullptr);
	}

	void CommandListBase::PipelineBarrier(
		VkPipelineStageFlags sourceStage,
		VkPipelineStageFlags destinationStage,
		VkDependencyFlags dependencyFlags,
		std::span<const VkMemoryBarrier> memoryBarriers,
		std::span<const VkBufferMemoryBarrier> bufferBarriers,
		std::span<const VkImageMemoryBarrier> imageBarriers) const
	{
		CORE_ASSERT(IsValid(), "Cannot record a barrier into an invalid command list");

		vkCmdPipelineBarrier(
			GetNative(),
			sourceStage,
			destinationStage,
			dependencyFlags,
			static_cast<uint32_t>(memoryBarriers.size()),
			memoryBarriers.data(),
			static_cast<uint32_t>(bufferBarriers.size()),
			bufferBarriers.data(),
			static_cast<uint32_t>(imageBarriers.size()),
			imageBarriers.data());
	}

	void CommandListBase::PushConstants(
		VkPipelineLayout pipelineLayout,
		VkShaderStageFlags shaderStages,
		std::span<const std::byte> data,
		uint32_t offset) const
	{
		CORE_ASSERT(IsValid(), "Cannot push constants into an invalid command list");
		CORE_ASSERT(pipelineLayout != VK_NULL_HANDLE, "PushConstants requires a pipeline layout");
		if (data.empty())
			return;

		vkCmdPushConstants(
			GetNative(),
			pipelineLayout,
			shaderStages,
			offset,
			static_cast<uint32_t>(data.size()),
			data.data());
	}
}
