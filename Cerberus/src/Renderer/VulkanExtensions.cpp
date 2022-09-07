#include "pch.h"
#include "VulkanExtensions.h"
#include <vulkan/vulkan_core.h>
namespace cerberus::ext
{
	/* NVVK_GENERATE_VERSION_INFO */
	// Generated using Vulkan 198
	/* NVVK_GENERATE_VERSION_INFO */
#ifdef VK_KHR_deferred_host_operations
	static PFN_vkCreateDeferredOperationKHR pfn_vkCreateDeferredOperationKHR = 0;
	static PFN_vkDeferredOperationJoinKHR pfn_vkDeferredOperationJoinKHR = 0;
	static PFN_vkDestroyDeferredOperationKHR pfn_vkDestroyDeferredOperationKHR = 0;
	static PFN_vkGetDeferredOperationMaxConcurrencyKHR pfn_vkGetDeferredOperationMaxConcurrencyKHR = 0;
	static PFN_vkGetDeferredOperationResultKHR pfn_vkGetDeferredOperationResultKHR = 0;
#endif /* VK_KHR_deferred_host_operations */

#ifdef VK_KHR_ray_tracing_pipeline
	static PFN_vkCmdSetRayTracingPipelineStackSizeKHR pfn_vkCmdSetRayTracingPipelineStackSizeKHR = 0;
	static PFN_vkCmdTraceRaysIndirectKHR pfn_vkCmdTraceRaysIndirectKHR = 0;
	static PFN_vkCmdTraceRaysKHR pfn_vkCmdTraceRaysKHR = 0;
	static PFN_vkCreateRayTracingPipelinesKHR pfn_vkCreateRayTracingPipelinesKHR = 0;
	static PFN_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR pfn_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR = 0;
	static PFN_vkGetRayTracingShaderGroupHandlesKHR pfn_vkGetRayTracingShaderGroupHandlesKHR = 0;
	static PFN_vkGetRayTracingShaderGroupStackSizeKHR pfn_vkGetRayTracingShaderGroupStackSizeKHR = 0;
#endif /* VK_KHR_ray_tracing_pipeline */
#ifdef VK_NV_mesh_shader
	static PFN_vkCmdDrawMeshTasksIndirectCountNV pfn_vkCmdDrawMeshTasksIndirectCountNV = 0;
	static PFN_vkCmdDrawMeshTasksIndirectNV pfn_vkCmdDrawMeshTasksIndirectNV = 0;
	static PFN_vkCmdDrawMeshTasksNV pfn_vkCmdDrawMeshTasksNV = 0;
#endif /* VK_NV_mesh_shader */
#ifdef VK_NV_ray_tracing
	static PFN_vkBindAccelerationStructureMemoryNV pfn_vkBindAccelerationStructureMemoryNV = 0;
	static PFN_vkCmdBuildAccelerationStructureNV pfn_vkCmdBuildAccelerationStructureNV = 0;
	static PFN_vkCmdCopyAccelerationStructureNV pfn_vkCmdCopyAccelerationStructureNV = 0;
	static PFN_vkCmdTraceRaysNV pfn_vkCmdTraceRaysNV = 0;
	static PFN_vkCmdWriteAccelerationStructuresPropertiesNV pfn_vkCmdWriteAccelerationStructuresPropertiesNV = 0;
	static PFN_vkCompileDeferredNV pfn_vkCompileDeferredNV = 0;
	static PFN_vkCreateAccelerationStructureNV pfn_vkCreateAccelerationStructureNV = 0;
	static PFN_vkCreateRayTracingPipelinesNV pfn_vkCreateRayTracingPipelinesNV = 0;
	static PFN_vkDestroyAccelerationStructureNV pfn_vkDestroyAccelerationStructureNV = 0;
	static PFN_vkGetAccelerationStructureHandleNV pfn_vkGetAccelerationStructureHandleNV = 0;
	static PFN_vkGetAccelerationStructureMemoryRequirementsNV pfn_vkGetAccelerationStructureMemoryRequirementsNV = 0;
	static PFN_vkGetRayTracingShaderGroupHandlesNV pfn_vkGetRayTracingShaderGroupHandlesNV = 0;
#endif /* VK_NV_ray_tracing */
#ifdef VK_KHR_acceleration_structure
	static PFN_vkBuildAccelerationStructuresKHR pfn_vkBuildAccelerationStructuresKHR = 0;
	static PFN_vkCmdBuildAccelerationStructuresIndirectKHR pfn_vkCmdBuildAccelerationStructuresIndirectKHR = 0;
	static PFN_vkCmdBuildAccelerationStructuresKHR pfn_vkCmdBuildAccelerationStructuresKHR = 0;
	static PFN_vkCmdCopyAccelerationStructureKHR pfn_vkCmdCopyAccelerationStructureKHR = 0;
	static PFN_vkCmdCopyAccelerationStructureToMemoryKHR pfn_vkCmdCopyAccelerationStructureToMemoryKHR = 0;
	static PFN_vkCmdCopyMemoryToAccelerationStructureKHR pfn_vkCmdCopyMemoryToAccelerationStructureKHR = 0;
	static PFN_vkCmdWriteAccelerationStructuresPropertiesKHR pfn_vkCmdWriteAccelerationStructuresPropertiesKHR = 0;
	static PFN_vkCopyAccelerationStructureKHR pfn_vkCopyAccelerationStructureKHR = 0;
	static PFN_vkCopyAccelerationStructureToMemoryKHR pfn_vkCopyAccelerationStructureToMemoryKHR = 0;
	static PFN_vkCopyMemoryToAccelerationStructureKHR pfn_vkCopyMemoryToAccelerationStructureKHR = 0;
	static PFN_vkCreateAccelerationStructureKHR pfn_vkCreateAccelerationStructureKHR = 0;
	static PFN_vkDestroyAccelerationStructureKHR pfn_vkDestroyAccelerationStructureKHR = 0;
	static PFN_vkGetAccelerationStructureBuildSizesKHR pfn_vkGetAccelerationStructureBuildSizesKHR = 0;
	static PFN_vkGetAccelerationStructureDeviceAddressKHR pfn_vkGetAccelerationStructureDeviceAddressKHR = 0;
	static PFN_vkGetDeviceAccelerationStructureCompatibilityKHR pfn_vkGetDeviceAccelerationStructureCompatibilityKHR = 0;
	static PFN_vkWriteAccelerationStructuresPropertiesKHR pfn_vkWriteAccelerationStructuresPropertiesKHR = 0;
#endif /* VK_KHR_acceleration_structure */


#ifdef VK_KHR_acceleration_structure
	VKAPI_ATTR VkResult VKAPI_CALL vkBuildAccelerationStructuresKHR(
		VkDevice                                           device,
		VkDeferredOperationKHR deferredOperation,
		uint32_t infoCount,
		const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
		const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
	{
		return pfn_vkBuildAccelerationStructuresKHR(device, deferredOperation, infoCount, pInfos, ppBuildRangeInfos);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresIndirectKHR(
		VkCommandBuffer                  commandBuffer,
		uint32_t                                           infoCount,
		const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
		const VkDeviceAddress* pIndirectDeviceAddresses,
		const uint32_t* pIndirectStrides,
		const uint32_t* const* ppMaxPrimitiveCounts)
	{
		pfn_vkCmdBuildAccelerationStructuresIndirectKHR(commandBuffer, infoCount, pInfos, pIndirectDeviceAddresses, pIndirectStrides, ppMaxPrimitiveCounts);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresKHR(
		VkCommandBuffer                                    commandBuffer,
		uint32_t infoCount,
		const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
		const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
	{
		pfn_vkCmdBuildAccelerationStructuresKHR(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureKHR(
		VkCommandBuffer commandBuffer,
		const VkCopyAccelerationStructureInfoKHR* pInfo)
	{
		pfn_vkCmdCopyAccelerationStructureKHR(commandBuffer, pInfo);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureToMemoryKHR(
		VkCommandBuffer commandBuffer,
		const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo)
	{
		pfn_vkCmdCopyAccelerationStructureToMemoryKHR(commandBuffer, pInfo);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToAccelerationStructureKHR(
		VkCommandBuffer commandBuffer,
		const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo)
	{
		pfn_vkCmdCopyMemoryToAccelerationStructureKHR(commandBuffer, pInfo);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesKHR(
		VkCommandBuffer commandBuffer,
		uint32_t accelerationStructureCount,
		const VkAccelerationStructureKHR* pAccelerationStructures,
		VkQueryType queryType,
		VkQueryPool queryPool,
		uint32_t firstQuery)
	{
		pfn_vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureKHR(
		VkDevice device,
		VkDeferredOperationKHR deferredOperation,
		const VkCopyAccelerationStructureInfoKHR* pInfo)
	{
		return pfn_vkCopyAccelerationStructureKHR(device, deferredOperation, pInfo);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureToMemoryKHR(
		VkDevice device,
		VkDeferredOperationKHR deferredOperation,
		const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo)
	{
		return pfn_vkCopyAccelerationStructureToMemoryKHR(device, deferredOperation, pInfo);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToAccelerationStructureKHR(
		VkDevice device,
		VkDeferredOperationKHR deferredOperation,
		const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo)
	{
		return pfn_vkCopyMemoryToAccelerationStructureKHR(device, deferredOperation, pInfo);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureKHR(
		VkDevice                                           device,
		const VkAccelerationStructureCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkAccelerationStructureKHR* pAccelerationStructure)
	{
		return pfn_vkCreateAccelerationStructureKHR(device, pCreateInfo, pAllocator, pAccelerationStructure);
	}
	VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureKHR(
		VkDevice device,
		VkAccelerationStructureKHR accelerationStructure,
		const VkAllocationCallbacks* pAllocator)
	{
		pfn_vkDestroyAccelerationStructureKHR(device, accelerationStructure, pAllocator);
	}
	VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureBuildSizesKHR(
		VkDevice                                            device,
		VkAccelerationStructureBuildTypeKHR                 buildType,
		const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo,
		const uint32_t* pMaxPrimitiveCounts,
		VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo)
	{
		pfn_vkGetAccelerationStructureBuildSizesKHR(device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo);
	}
	VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetAccelerationStructureDeviceAddressKHR(
		VkDevice device,
		const VkAccelerationStructureDeviceAddressInfoKHR* pInfo)
	{
		return pfn_vkGetAccelerationStructureDeviceAddressKHR(device, pInfo);
	}
	VKAPI_ATTR void VKAPI_CALL vkGetDeviceAccelerationStructureCompatibilityKHR(
		VkDevice device,
		const VkAccelerationStructureVersionInfoKHR* pVersionInfo,
		VkAccelerationStructureCompatibilityKHR* pCompatibility)
	{
		pfn_vkGetDeviceAccelerationStructureCompatibilityKHR(device, pVersionInfo, pCompatibility);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkWriteAccelerationStructuresPropertiesKHR(
		VkDevice device,
		uint32_t accelerationStructureCount,
		const VkAccelerationStructureKHR* pAccelerationStructures,
		VkQueryType  queryType,
		size_t       dataSize,
		void* pData,
		size_t stride)
	{
		return pfn_vkWriteAccelerationStructuresPropertiesKHR(device, accelerationStructureCount, pAccelerationStructures, queryType, dataSize, pData, stride);
	}
#endif /* VK_KHR_acceleration_structure */

#ifdef VK_KHR_deferred_host_operations
	VKAPI_ATTR VkResult VKAPI_CALL vkCreateDeferredOperationKHR(
		VkDevice device,
		const VkAllocationCallbacks* pAllocator,
		VkDeferredOperationKHR* pDeferredOperation)
	{
		return pfn_vkCreateDeferredOperationKHR(device, pAllocator, pDeferredOperation);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkDeferredOperationJoinKHR(
		VkDevice device,
		VkDeferredOperationKHR operation)
	{
		return pfn_vkDeferredOperationJoinKHR(device, operation);
	}
	VKAPI_ATTR void VKAPI_CALL vkDestroyDeferredOperationKHR(
		VkDevice device,
		VkDeferredOperationKHR operation,
		const VkAllocationCallbacks* pAllocator)
	{
		pfn_vkDestroyDeferredOperationKHR(device, operation, pAllocator);
	}
	VKAPI_ATTR uint32_t VKAPI_CALL vkGetDeferredOperationMaxConcurrencyKHR(
		VkDevice device,
		VkDeferredOperationKHR operation)
	{
		return pfn_vkGetDeferredOperationMaxConcurrencyKHR(device, operation);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkGetDeferredOperationResultKHR(
		VkDevice device,
		VkDeferredOperationKHR operation)
	{
		return pfn_vkGetDeferredOperationResultKHR(device, operation);
	}
#endif /* VK_KHR_deferred_host_operations */


#ifdef VK_KHR_ray_tracing_pipeline
	VKAPI_ATTR void VKAPI_CALL vkCmdSetRayTracingPipelineStackSizeKHR(
		VkCommandBuffer commandBuffer,
		uint32_t pipelineStackSize)
	{
		pfn_vkCmdSetRayTracingPipelineStackSizeKHR(commandBuffer, pipelineStackSize);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysIndirectKHR(
		VkCommandBuffer commandBuffer,
		const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
		VkDeviceAddress indirectDeviceAddress)
	{
		pfn_vkCmdTraceRaysIndirectKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, indirectDeviceAddress);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysKHR(
		VkCommandBuffer commandBuffer,
		const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
		uint32_t width,
		uint32_t height,
		uint32_t depth)
	{
		pfn_vkCmdTraceRaysKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesKHR(
		VkDevice device,
		VkDeferredOperationKHR deferredOperation,
		VkPipelineCache pipelineCache,
		uint32_t createInfoCount,
		const VkRayTracingPipelineCreateInfoKHR* pCreateInfos,
		const VkAllocationCallbacks* pAllocator,
		VkPipeline* pPipelines)
	{
		return pfn_vkCreateRayTracingPipelinesKHR(device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(
		VkDevice device,
		VkPipeline pipeline,
		uint32_t firstGroup,
		uint32_t groupCount,
		size_t dataSize,
		void* pData)
	{
		return pfn_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(device, pipeline, firstGroup, groupCount, dataSize, pData);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesKHR(
		VkDevice device,
		VkPipeline pipeline,
		uint32_t firstGroup,
		uint32_t groupCount,
		size_t dataSize,
		void* pData)
	{
		return pfn_vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, firstGroup, groupCount, dataSize, pData);
	}
	VKAPI_ATTR VkDeviceSize VKAPI_CALL vkGetRayTracingShaderGroupStackSizeKHR(
		VkDevice device,
		VkPipeline pipeline,
		uint32_t group,
		VkShaderGroupShaderKHR groupShader)
	{
		return pfn_vkGetRayTracingShaderGroupStackSizeKHR(device, pipeline, group, groupShader);
	}
#endif /* VK_KHR_ray_tracing_pipeline */

#ifdef VK_NV_mesh_shader
	VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountNV(
		VkCommandBuffer commandBuffer,
		VkBuffer buffer,
		VkDeviceSize offset,
		VkBuffer countBuffer,
		VkDeviceSize countBufferOffset,
		uint32_t maxDrawCount,
		uint32_t stride)
	{
		pfn_vkCmdDrawMeshTasksIndirectCountNV(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectNV(
		VkCommandBuffer commandBuffer,
		VkBuffer buffer,
		VkDeviceSize offset,
		uint32_t drawCount,
		uint32_t stride)
	{
		pfn_vkCmdDrawMeshTasksIndirectNV(commandBuffer, buffer, offset, drawCount, stride);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksNV(
		VkCommandBuffer commandBuffer,
		uint32_t taskCount,
		uint32_t firstTask)
	{
		pfn_vkCmdDrawMeshTasksNV(commandBuffer, taskCount, firstTask);
	}
#endif /* VK_NV_mesh_shader */
#ifdef VK_NV_ray_tracing
	VKAPI_ATTR VkResult VKAPI_CALL vkBindAccelerationStructureMemoryNV(
		VkDevice device,
		uint32_t bindInfoCount,
		const VkBindAccelerationStructureMemoryInfoNV* pBindInfos)
	{
		return pfn_vkBindAccelerationStructureMemoryNV(device, bindInfoCount, pBindInfos);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructureNV(
		VkCommandBuffer commandBuffer,
		const VkAccelerationStructureInfoNV* pInfo,
		VkBuffer instanceData,
		VkDeviceSize instanceOffset,
		VkBool32 update,
		VkAccelerationStructureNV dst,
		VkAccelerationStructureNV src,
		VkBuffer scratch,
		VkDeviceSize scratchOffset)
	{
		pfn_vkCmdBuildAccelerationStructureNV(commandBuffer, pInfo, instanceData, instanceOffset, update, dst, src, scratch, scratchOffset);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureNV(
		VkCommandBuffer commandBuffer,
		VkAccelerationStructureNV dst,
		VkAccelerationStructureNV src,
		VkCopyAccelerationStructureModeKHR mode)
	{
		pfn_vkCmdCopyAccelerationStructureNV(commandBuffer, dst, src, mode);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysNV(
		VkCommandBuffer commandBuffer,
		VkBuffer raygenShaderBindingTableBuffer,
		VkDeviceSize raygenShaderBindingOffset,
		VkBuffer missShaderBindingTableBuffer,
		VkDeviceSize missShaderBindingOffset,
		VkDeviceSize missShaderBindingStride,
		VkBuffer hitShaderBindingTableBuffer,
		VkDeviceSize hitShaderBindingOffset,
		VkDeviceSize hitShaderBindingStride,
		VkBuffer callableShaderBindingTableBuffer,
		VkDeviceSize callableShaderBindingOffset,
		VkDeviceSize callableShaderBindingStride,
		uint32_t width,
		uint32_t height,
		uint32_t depth)
	{
		pfn_vkCmdTraceRaysNV(commandBuffer, raygenShaderBindingTableBuffer, raygenShaderBindingOffset, missShaderBindingTableBuffer, missShaderBindingOffset, missShaderBindingStride, hitShaderBindingTableBuffer, hitShaderBindingOffset, hitShaderBindingStride, callableShaderBindingTableBuffer, callableShaderBindingOffset, callableShaderBindingStride, width, height, depth);
	}
	VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesNV(
		VkCommandBuffer commandBuffer,
		uint32_t accelerationStructureCount,
		const VkAccelerationStructureNV* pAccelerationStructures,
		VkQueryType queryType,
		VkQueryPool queryPool,
		uint32_t firstQuery)
	{
		pfn_vkCmdWriteAccelerationStructuresPropertiesNV(commandBuffer, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkCompileDeferredNV(
		VkDevice device,
		VkPipeline pipeline,
		uint32_t shader)
	{
		return pfn_vkCompileDeferredNV(device, pipeline, shader);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureNV(
		VkDevice device,
		const VkAccelerationStructureCreateInfoNV* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkAccelerationStructureNV* pAccelerationStructure)
	{
		return pfn_vkCreateAccelerationStructureNV(device, pCreateInfo, pAllocator, pAccelerationStructure);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesNV(
		VkDevice device,
		VkPipelineCache pipelineCache,
		uint32_t createInfoCount,
		const VkRayTracingPipelineCreateInfoNV* pCreateInfos,
		const VkAllocationCallbacks* pAllocator,
		VkPipeline* pPipelines)
	{
		return pfn_vkCreateRayTracingPipelinesNV(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
	}
	VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureNV(
		VkDevice device,
		VkAccelerationStructureNV accelerationStructure,
		const VkAllocationCallbacks* pAllocator)
	{
		pfn_vkDestroyAccelerationStructureNV(device, accelerationStructure, pAllocator);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkGetAccelerationStructureHandleNV(
		VkDevice device,
		VkAccelerationStructureNV accelerationStructure,
		size_t dataSize,
		void* pData)
	{
		return pfn_vkGetAccelerationStructureHandleNV(device, accelerationStructure, dataSize, pData);
	}
	VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureMemoryRequirementsNV(
		VkDevice device,
		const VkAccelerationStructureMemoryRequirementsInfoNV* pInfo,
		VkMemoryRequirements2KHR* pMemoryRequirements)
	{
		pfn_vkGetAccelerationStructureMemoryRequirementsNV(device, pInfo, pMemoryRequirements);
	}
	VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesNV(
		VkDevice device,
		VkPipeline pipeline,
		uint32_t firstGroup,
		uint32_t groupCount,
		size_t dataSize,
		void* pData)
	{
		return pfn_vkGetRayTracingShaderGroupHandlesNV(device, pipeline, firstGroup, groupCount, dataSize, pData);
	}
#endif /* VK_NV_ray_tracing */

	/* NVVK_GENERATE_DECLARE */


	/* super load/reset */
	void LoadVKExtensions(VkInstance instance, PFN_vkGetInstanceProcAddr getInstanceProcAddr, VkDevice device, PFN_vkGetDeviceProcAddr getDeviceProcAddr)
	{
		
#ifdef VK_KHR_acceleration_structure
		pfn_vkBuildAccelerationStructuresKHR = (PFN_vkBuildAccelerationStructuresKHR)getDeviceProcAddr(device, "vkBuildAccelerationStructuresKHR");
		pfn_vkCmdBuildAccelerationStructuresIndirectKHR = (PFN_vkCmdBuildAccelerationStructuresIndirectKHR)getDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresIndirectKHR");
		pfn_vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)getDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR");
		pfn_vkCmdCopyAccelerationStructureKHR = (PFN_vkCmdCopyAccelerationStructureKHR)getDeviceProcAddr(device, "vkCmdCopyAccelerationStructureKHR");
		pfn_vkCmdCopyAccelerationStructureToMemoryKHR = (PFN_vkCmdCopyAccelerationStructureToMemoryKHR)getDeviceProcAddr(device, "vkCmdCopyAccelerationStructureToMemoryKHR");
		pfn_vkCmdCopyMemoryToAccelerationStructureKHR = (PFN_vkCmdCopyMemoryToAccelerationStructureKHR)getDeviceProcAddr(device, "vkCmdCopyMemoryToAccelerationStructureKHR");
		pfn_vkCmdWriteAccelerationStructuresPropertiesKHR = (PFN_vkCmdWriteAccelerationStructuresPropertiesKHR)getDeviceProcAddr(device, "vkCmdWriteAccelerationStructuresPropertiesKHR");
		pfn_vkCopyAccelerationStructureKHR = (PFN_vkCopyAccelerationStructureKHR)getDeviceProcAddr(device, "vkCopyAccelerationStructureKHR");
		pfn_vkCopyAccelerationStructureToMemoryKHR = (PFN_vkCopyAccelerationStructureToMemoryKHR)getDeviceProcAddr(device, "vkCopyAccelerationStructureToMemoryKHR");
		pfn_vkCopyMemoryToAccelerationStructureKHR = (PFN_vkCopyMemoryToAccelerationStructureKHR)getDeviceProcAddr(device, "vkCopyMemoryToAccelerationStructureKHR");
		pfn_vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)getDeviceProcAddr(device, "vkCreateAccelerationStructureKHR");
		pfn_vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)getDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");
		pfn_vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)getDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR");
		pfn_vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)getDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR");
		pfn_vkGetDeviceAccelerationStructureCompatibilityKHR = (PFN_vkGetDeviceAccelerationStructureCompatibilityKHR)getDeviceProcAddr(device, "vkGetDeviceAccelerationStructureCompatibilityKHR");
		pfn_vkWriteAccelerationStructuresPropertiesKHR = (PFN_vkWriteAccelerationStructuresPropertiesKHR)getDeviceProcAddr(device, "vkWriteAccelerationStructuresPropertiesKHR");
#endif /* VK_KHR_acceleration_structure */

#ifdef VK_KHR_deferred_host_operations
		pfn_vkCreateDeferredOperationKHR = (PFN_vkCreateDeferredOperationKHR)getDeviceProcAddr(device, "vkCreateDeferredOperationKHR");
		pfn_vkDeferredOperationJoinKHR = (PFN_vkDeferredOperationJoinKHR)getDeviceProcAddr(device, "vkDeferredOperationJoinKHR");
		pfn_vkDestroyDeferredOperationKHR = (PFN_vkDestroyDeferredOperationKHR)getDeviceProcAddr(device, "vkDestroyDeferredOperationKHR");
		pfn_vkGetDeferredOperationMaxConcurrencyKHR = (PFN_vkGetDeferredOperationMaxConcurrencyKHR)getDeviceProcAddr(device, "vkGetDeferredOperationMaxConcurrencyKHR");
		pfn_vkGetDeferredOperationResultKHR = (PFN_vkGetDeferredOperationResultKHR)getDeviceProcAddr(device, "vkGetDeferredOperationResultKHR");
#endif /* VK_KHR_deferred_host_operations */

#ifdef VK_KHR_ray_tracing_pipeline
		pfn_vkCmdSetRayTracingPipelineStackSizeKHR = (PFN_vkCmdSetRayTracingPipelineStackSizeKHR)getDeviceProcAddr(device, "vkCmdSetRayTracingPipelineStackSizeKHR");
		pfn_vkCmdTraceRaysIndirectKHR = (PFN_vkCmdTraceRaysIndirectKHR)getDeviceProcAddr(device, "vkCmdTraceRaysIndirectKHR");
		pfn_vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)getDeviceProcAddr(device, "vkCmdTraceRaysKHR");
		pfn_vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)getDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR");
		pfn_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR = (PFN_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR)getDeviceProcAddr(device, "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR");
		pfn_vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)getDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR");
		pfn_vkGetRayTracingShaderGroupStackSizeKHR = (PFN_vkGetRayTracingShaderGroupStackSizeKHR)getDeviceProcAddr(device, "vkGetRayTracingShaderGroupStackSizeKHR");
#endif /* VK_KHR_ray_tracing_pipeline */

#ifdef VK_NV_mesh_shader
		pfn_vkCmdDrawMeshTasksIndirectCountNV = (PFN_vkCmdDrawMeshTasksIndirectCountNV)getDeviceProcAddr(device, "vkCmdDrawMeshTasksIndirectCountNV");
		pfn_vkCmdDrawMeshTasksIndirectNV = (PFN_vkCmdDrawMeshTasksIndirectNV)getDeviceProcAddr(device, "vkCmdDrawMeshTasksIndirectNV");
		pfn_vkCmdDrawMeshTasksNV = (PFN_vkCmdDrawMeshTasksNV)getDeviceProcAddr(device, "vkCmdDrawMeshTasksNV");
#endif /* VK_NV_mesh_shader */
#ifdef VK_NV_ray_tracing
		pfn_vkBindAccelerationStructureMemoryNV = (PFN_vkBindAccelerationStructureMemoryNV)getDeviceProcAddr(device, "vkBindAccelerationStructureMemoryNV");
		pfn_vkCmdBuildAccelerationStructureNV = (PFN_vkCmdBuildAccelerationStructureNV)getDeviceProcAddr(device, "vkCmdBuildAccelerationStructureNV");
		pfn_vkCmdCopyAccelerationStructureNV = (PFN_vkCmdCopyAccelerationStructureNV)getDeviceProcAddr(device, "vkCmdCopyAccelerationStructureNV");
		pfn_vkCmdTraceRaysNV = (PFN_vkCmdTraceRaysNV)getDeviceProcAddr(device, "vkCmdTraceRaysNV");
		pfn_vkCmdWriteAccelerationStructuresPropertiesNV = (PFN_vkCmdWriteAccelerationStructuresPropertiesNV)getDeviceProcAddr(device, "vkCmdWriteAccelerationStructuresPropertiesNV");
		pfn_vkCompileDeferredNV = (PFN_vkCompileDeferredNV)getDeviceProcAddr(device, "vkCompileDeferredNV");
		pfn_vkCreateAccelerationStructureNV = (PFN_vkCreateAccelerationStructureNV)getDeviceProcAddr(device, "vkCreateAccelerationStructureNV");
		pfn_vkCreateRayTracingPipelinesNV = (PFN_vkCreateRayTracingPipelinesNV)getDeviceProcAddr(device, "vkCreateRayTracingPipelinesNV");
		pfn_vkDestroyAccelerationStructureNV = (PFN_vkDestroyAccelerationStructureNV)getDeviceProcAddr(device, "vkDestroyAccelerationStructureNV");
		pfn_vkGetAccelerationStructureHandleNV = (PFN_vkGetAccelerationStructureHandleNV)getDeviceProcAddr(device, "vkGetAccelerationStructureHandleNV");
		pfn_vkGetAccelerationStructureMemoryRequirementsNV = (PFN_vkGetAccelerationStructureMemoryRequirementsNV)getDeviceProcAddr(device, "vkGetAccelerationStructureMemoryRequirementsNV");
		pfn_vkGetRayTracingShaderGroupHandlesNV = (PFN_vkGetRayTracingShaderGroupHandlesNV)getDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesNV");
#endif /* VK_NV_ray_tracing */
		/* NVVK_GENERATE_LOAD_PROC */
	}
}