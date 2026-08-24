#pragma once

#include "Core/Handle.h"
#include "Renderer/RHI/Command/CommandList.h"

namespace cerberus
{
	class BindGroup;
	class RayTracingPipeline;
	class ShaderBindingTable;

	// Typed ray-tracing recording view. It borrows an active recording and never
	// allocates, submits or synchronizes the underlying command buffer.
	class RayTracingCommandList final : public CommandListBase
	{
	public:
		explicit RayTracingCommandList(
			CommandBufferView commandBuffer,
			RendererFrameStatistics* statistics = nullptr);

		void BindRayTracingPipeline(Handle<RayTracingPipeline> pipeline);
		void BindDescriptorSets(Handle<BindGroup> bindGroup, uint32_t firstSet = 0);
		void BindDescriptorSets(
			std::span<const Handle<BindGroup>> bindGroups,
			uint32_t firstSet = 0);
		void BindDescriptorSets(
			std::span<const VkDescriptorSet> descriptorSets,
			uint32_t firstSet = 0);
		void SetPushConstants(
			VkShaderStageFlags shaderStages,
			std::span<const std::byte> data,
			uint32_t offset = 0);
		void TraceRays(
			const ShaderBindingTable& shaderBindingTable,
			uint32_t width,
			uint32_t height,
			uint32_t depth = 1);

		void BuildAccelerationStructure(
			const VkAccelerationStructureBuildGeometryInfoKHR& buildInfo,
			const VkAccelerationStructureBuildRangeInfoKHR& rangeInfo) const;
		void CopyAccelerationStructure(
			const VkCopyAccelerationStructureInfoKHR& copyInfo) const;
		void WriteAccelerationStructureProperties(
			VkAccelerationStructureKHR accelerationStructure,
			VkQueryType queryType,
			VkQueryPool queryPool,
			uint32_t firstQuery) const;

	private:
		VkPipelineLayout m_BoundPipelineLayout = VK_NULL_HANDLE;
	};
}
