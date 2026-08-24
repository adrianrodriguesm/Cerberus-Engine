#include "pch.h"
#include "RayTracingCommandList.h"

#include "Renderer/RHI/Pipeline/Pipeline.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/RayTracing/ShaderBindingTable.h"
#include "Renderer/RHI/VulkanExtensions.h"

namespace cerberus
{
	RayTracingCommandList::RayTracingCommandList(
		CommandBufferView commandBuffer,
		RendererFrameStatistics* statistics)
		: CommandListBase(commandBuffer, statistics)
	{
	}

	void RayTracingCommandList::BindRayTracingPipeline(Handle<RayTracingPipeline> pipelineHandle)
	{
		RayTracingPipeline* pipeline = ResourceManager::GetRayTracingPipeline(pipelineHandle);
		CORE_ASSERT(pipeline && pipeline->GetHandle() != VK_NULL_HANDLE, "Invalid ray-tracing pipeline handle");

		vkCmdBindPipeline(GetNative(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->GetHandle());
		m_BoundPipelineLayout = pipeline->GetVulkanPipelineLayout();
	}

	void RayTracingCommandList::BindDescriptorSets(Handle<BindGroup> bindGroupHandle, uint32_t firstSet)
	{
		BindDescriptorSetInternal(
			VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
			m_BoundPipelineLayout,
			bindGroupHandle,
			firstSet);
	}

	void RayTracingCommandList::BindDescriptorSets(
		std::span<const Handle<BindGroup>> bindGroups,
		uint32_t firstSet)
	{
		BindDescriptorSetsInternal(
			VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
			m_BoundPipelineLayout,
			bindGroups,
			firstSet);
	}

	void RayTracingCommandList::BindDescriptorSets(
		std::span<const VkDescriptorSet> descriptorSets,
		uint32_t firstSet)
	{
		BindDescriptorSetsInternal(
			VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
			m_BoundPipelineLayout,
			descriptorSets,
			firstSet);
	}

	void RayTracingCommandList::SetPushConstants(
		VkShaderStageFlags shaderStages,
		std::span<const std::byte> data,
		uint32_t offset)
	{
		CORE_ASSERT(m_BoundPipelineLayout != VK_NULL_HANDLE, "Bind a ray-tracing pipeline before push constants");
		PushConstants(m_BoundPipelineLayout, shaderStages, data, offset);
	}

	void RayTracingCommandList::TraceRays(
		const ShaderBindingTable& shaderBindingTable,
		uint32_t width,
		uint32_t height,
		uint32_t depth)
	{
		CORE_ASSERT(m_BoundPipelineLayout != VK_NULL_HANDLE, "Bind a ray-tracing pipeline before TraceRays");
		CORE_ASSERT(width > 0 && height > 0 && depth > 0, "TraceRays dimensions must be non-zero");

		const ShaderBindingTableRegions& regions = shaderBindingTable.GetRegions();
		CORE_ASSERT(regions.RayGen.deviceAddress != 0, "TraceRays requires a valid ray-generation SBT region");
		ext::vkCmdTraceRaysKHR(
			GetNative(),
			&regions.RayGen,
			&regions.Miss,
			&regions.Hit,
			&regions.Callable,
			width,
			height,
			depth);

		if (RendererFrameStatistics* statistics = GetStatistics())
			++statistics->RayTracingDispatchCount;
	}

	void RayTracingCommandList::BuildAccelerationStructure(
		const VkAccelerationStructureBuildGeometryInfoKHR& buildInfo,
		const VkAccelerationStructureBuildRangeInfoKHR& rangeInfo) const
	{
		const VkAccelerationStructureBuildRangeInfoKHR* rangeInfoPointer = &rangeInfo;
		ext::vkCmdBuildAccelerationStructuresKHR(
			GetNative(),
			1,
			&buildInfo,
			&rangeInfoPointer);
	}

	void RayTracingCommandList::CopyAccelerationStructure(
		const VkCopyAccelerationStructureInfoKHR& copyInfo) const
	{
		ext::vkCmdCopyAccelerationStructureKHR(GetNative(), &copyInfo);
	}

	void RayTracingCommandList::WriteAccelerationStructureProperties(
		VkAccelerationStructureKHR accelerationStructure,
		VkQueryType queryType,
		VkQueryPool queryPool,
		uint32_t firstQuery) const
	{
		CORE_ASSERT(accelerationStructure != VK_NULL_HANDLE,
			"WriteAccelerationStructureProperties requires a valid acceleration structure");
		CORE_ASSERT(queryPool != VK_NULL_HANDLE,
			"WriteAccelerationStructureProperties requires a valid query pool");

		ext::vkCmdWriteAccelerationStructuresPropertiesKHR(
			GetNative(),
			1,
			&accelerationStructure,
			queryType,
			queryPool,
			firstQuery);
	}
}
