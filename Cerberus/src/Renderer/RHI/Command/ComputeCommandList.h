#pragma once

#include "Core/Handle.h"
#include "Renderer/RHI/Command/CommandList.h"

namespace cerberus
{
	class BindGroup;
	class ComputePipeline;

	// Typed compute recording view. Pipeline and submission lifetime stay external.
	class ComputeCommandList final : public CommandListBase
	{
	public:
		explicit ComputeCommandList(
			CommandBufferView commandBuffer,
			RendererFrameStatistics* statistics = nullptr);

		void BindPipeline(Handle<ComputePipeline> pipeline);
		void BindDescriptorSets(Handle<BindGroup> bindGroup, uint32_t firstSet = 0);
		void BindDescriptorSets(
			std::span<const Handle<BindGroup>> bindGroups,
			uint32_t firstSet = 0);
		void BindDescriptorSets(
			std::span<const VkDescriptorSet> descriptorSets,
			uint32_t firstSet = 0);
		void SetPushConstants(std::span<const std::byte> data, uint32_t offset = 0);
		void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

	private:
		VkPipelineLayout m_BoundPipelineLayout = VK_NULL_HANDLE;
	};
}
