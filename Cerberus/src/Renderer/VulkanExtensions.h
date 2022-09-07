#pragma once

namespace cerberus::ext
{
#ifdef VK_KHR_acceleration_structure
	VKAPI_ATTR VkResult VKAPI_CALL vkBuildAccelerationStructuresKHR(
		VkDevice                                           device,
		VkDeferredOperationKHR deferredOperation,
		uint32_t infoCount,
		const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
		const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos);

	VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresIndirectKHR(
		VkCommandBuffer                  commandBuffer,
		uint32_t                                           infoCount,
		const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
		const VkDeviceAddress* pIndirectDeviceAddresses,
		const uint32_t* pIndirectStrides,
		const uint32_t* const* ppMaxPrimitiveCounts);

	VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresKHR(
		VkCommandBuffer                                    commandBuffer,
		uint32_t infoCount,
		const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
		const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos);

	VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureKHR(
		VkCommandBuffer commandBuffer,
		const VkCopyAccelerationStructureInfoKHR* pInfo);

	VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureToMemoryKHR(
		VkCommandBuffer commandBuffer,
		const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo);

	VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToAccelerationStructureKHR(
		VkCommandBuffer commandBuffer,
		const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo);

	VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesKHR(
		VkCommandBuffer commandBuffer,
		uint32_t accelerationStructureCount,
		const VkAccelerationStructureKHR* pAccelerationStructures,
		VkQueryType queryType,
		VkQueryPool queryPool,
		uint32_t firstQuery);

	VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureKHR(
		VkDevice device,
		VkDeferredOperationKHR deferredOperation,
		const VkCopyAccelerationStructureInfoKHR* pInfo);

	VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureToMemoryKHR(
		VkDevice device,
		VkDeferredOperationKHR deferredOperation,
		const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo);

	VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToAccelerationStructureKHR(
		VkDevice device,
		VkDeferredOperationKHR deferredOperation,
		const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo);

	VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureKHR(
		VkDevice                                           device,
		const VkAccelerationStructureCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkAccelerationStructureKHR* pAccelerationStructure);

	VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureKHR(
		VkDevice device,
		VkAccelerationStructureKHR accelerationStructure,
		const VkAllocationCallbacks* pAllocator);

	VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureBuildSizesKHR(
		VkDevice                                            device,
		VkAccelerationStructureBuildTypeKHR                 buildType,
		const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo,
		const uint32_t* pMaxPrimitiveCounts,
		VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo);

	VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetAccelerationStructureDeviceAddressKHR(
		VkDevice device,
		const VkAccelerationStructureDeviceAddressInfoKHR* pInfo);

	VKAPI_ATTR void VKAPI_CALL vkGetDeviceAccelerationStructureCompatibilityKHR(
		VkDevice device,
		const VkAccelerationStructureVersionInfoKHR* pVersionInfo,
		VkAccelerationStructureCompatibilityKHR* pCompatibility);

	VKAPI_ATTR VkResult VKAPI_CALL vkWriteAccelerationStructuresPropertiesKHR(
		VkDevice device,
		uint32_t accelerationStructureCount,
		const VkAccelerationStructureKHR* pAccelerationStructures,
		VkQueryType  queryType,
		size_t       dataSize,
		void* pData,
		size_t stride);

#endif /* VK_KHR_acceleration_structure */
#ifdef VK_KHR_deferred_host_operations
	VKAPI_ATTR VkResult VKAPI_CALL vkCreateDeferredOperationKHR(
		VkDevice device,
		const VkAllocationCallbacks* pAllocator,
		VkDeferredOperationKHR* pDeferredOperation);

	VKAPI_ATTR VkResult VKAPI_CALL vkDeferredOperationJoinKHR(
		VkDevice device,
		VkDeferredOperationKHR operation);

	VKAPI_ATTR void VKAPI_CALL vkDestroyDeferredOperationKHR(
		VkDevice device,
		VkDeferredOperationKHR operation,
		const VkAllocationCallbacks* pAllocator);

	VKAPI_ATTR uint32_t VKAPI_CALL vkGetDeferredOperationMaxConcurrencyKHR(
		VkDevice device,
		VkDeferredOperationKHR operation);

	VKAPI_ATTR VkResult VKAPI_CALL vkGetDeferredOperationResultKHR(
		VkDevice device,
		VkDeferredOperationKHR operation);
#endif /* VK_KHR_deferred_host_operations */
#ifdef VK_KHR_ray_tracing_pipeline
	VKAPI_ATTR void VKAPI_CALL vkCmdSetRayTracingPipelineStackSizeKHR(
		VkCommandBuffer commandBuffer,
		uint32_t pipelineStackSize);

	VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysIndirectKHR(
		VkCommandBuffer commandBuffer,
		const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
		VkDeviceAddress indirectDeviceAddress);

	VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysKHR(
		VkCommandBuffer commandBuffer,
		const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
		uint32_t width,
		uint32_t height,
		uint32_t depth);

	VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesKHR(
		VkDevice device,
		VkDeferredOperationKHR deferredOperation,
		VkPipelineCache pipelineCache,
		uint32_t createInfoCount,
		const VkRayTracingPipelineCreateInfoKHR* pCreateInfos,
		const VkAllocationCallbacks* pAllocator,
		VkPipeline* pPipelines);

	VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(
		VkDevice device,
		VkPipeline pipeline,
		uint32_t firstGroup,
		uint32_t groupCount,
		size_t dataSize,
		void* pData);

	VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesKHR(
		VkDevice device,
		VkPipeline pipeline,
		uint32_t firstGroup,
		uint32_t groupCount,
		size_t dataSize,
		void* pData);

	VKAPI_ATTR VkDeviceSize VKAPI_CALL vkGetRayTracingShaderGroupStackSizeKHR(
		VkDevice device,
		VkPipeline pipeline,
		uint32_t group,
		VkShaderGroupShaderKHR groupShader);

#endif /* VK_KHR_ray_tracing_pipeline */

#ifdef VK_NV_mesh_shader
	VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountNV(
		VkCommandBuffer commandBuffer,
		VkBuffer buffer,
		VkDeviceSize offset,
		VkBuffer countBuffer,
		VkDeviceSize countBufferOffset,
		uint32_t maxDrawCount,
		uint32_t stride);

	VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectNV(
		VkCommandBuffer commandBuffer,
		VkBuffer buffer,
		VkDeviceSize offset,
		uint32_t drawCount,
		uint32_t stride);

	VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksNV(
		VkCommandBuffer commandBuffer,
		uint32_t taskCount,
		uint32_t firstTask);

#endif /* VK_NV_mesh_shader */
#ifdef VK_NV_ray_tracing
	VKAPI_ATTR VkResult VKAPI_CALL vkBindAccelerationStructureMemoryNV(
		VkDevice device,
		uint32_t bindInfoCount,
		const VkBindAccelerationStructureMemoryInfoNV* pBindInfos);

	VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructureNV(
		VkCommandBuffer commandBuffer,
		const VkAccelerationStructureInfoNV* pInfo,
		VkBuffer instanceData,
		VkDeviceSize instanceOffset,
		VkBool32 update,
		VkAccelerationStructureNV dst,
		VkAccelerationStructureNV src,
		VkBuffer scratch,
		VkDeviceSize scratchOffset);

	VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureNV(
		VkCommandBuffer commandBuffer,
		VkAccelerationStructureNV dst,
		VkAccelerationStructureNV src,
		VkCopyAccelerationStructureModeKHR mode);

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
		uint32_t depth);

	VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesNV(
		VkCommandBuffer commandBuffer,
		uint32_t accelerationStructureCount,
		const VkAccelerationStructureNV* pAccelerationStructures,
		VkQueryType queryType,
		VkQueryPool queryPool,
		uint32_t firstQuery);

	VKAPI_ATTR VkResult VKAPI_CALL vkCompileDeferredNV(
		VkDevice device,
		VkPipeline pipeline,
		uint32_t shader);

	VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureNV(
		VkDevice device,
		const VkAccelerationStructureCreateInfoNV* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkAccelerationStructureNV* pAccelerationStructure);

	VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesNV(
		VkDevice device,
		VkPipelineCache pipelineCache,
		uint32_t createInfoCount,
		const VkRayTracingPipelineCreateInfoNV* pCreateInfos,
		const VkAllocationCallbacks* pAllocator,
		VkPipeline* pPipelines);

	VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureNV(
		VkDevice device,
		VkAccelerationStructureNV accelerationStructure,
		const VkAllocationCallbacks* pAllocator);

	VKAPI_ATTR VkResult VKAPI_CALL vkGetAccelerationStructureHandleNV(
		VkDevice device,
		VkAccelerationStructureNV accelerationStructure,
		size_t dataSize,
		void* pData);

	VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureMemoryRequirementsNV(
		VkDevice device,
		const VkAccelerationStructureMemoryRequirementsInfoNV* pInfo,
		VkMemoryRequirements2KHR* pMemoryRequirements);

	VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesNV(
		VkDevice device,
		VkPipeline pipeline,
		uint32_t firstGroup,
		uint32_t groupCount,
		size_t dataSize,
		void* pData);
#endif /* VK_NV_ray_tracing */

	// NOTE: This implemantain was copied from 
	// Load all available extensions
	void LoadVKExtensions(VkInstance instance, PFN_vkGetInstanceProcAddr getInstanceProcAddr, VkDevice device, PFN_vkGetDeviceProcAddr getDeviceProcAddr);

}