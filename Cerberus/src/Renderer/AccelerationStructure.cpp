#include "pch.h"
#include "AccelerationStructure.h"
#include "Renderer/RendererContext.h"
#include "Renderer/Renderer.h"
#include "Renderer/VulkanExtensions.h"
namespace cerberus
{

#define AS_LOG_INFO(...) NV_CORE_INFO(__VA_ARGS__)

	uint64_t RoundUp(uint64_t size, uint64_t granularity)
	{
		const auto divUp = (size + granularity - 1) / granularity;
		return divUp * granularity;
	}

	namespace utils
	{
		VkAccelerationStructureTypeKHR CerberusASTypeToVulkanASType(AccelerationStructureType type)
		{
			switch (type)
			{
			case AccelerationStructureType::BottomLevel: return VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			case AccelerationStructureType::TopLevel:	 return VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
			default: CORE_ASSERT(false, "Unknow Acceleration Structure type"); return VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
			}
		}

		inline uint64_t RoundUp(uint64_t size, uint64_t granularity)
		{
			const auto divUp = (size + granularity - 1) / granularity;
			return divUp * granularity;
		}

		inline VkTransformMatrixKHR TransformFromGLMToKHRMatrix(const glm::mat4& matrix)
		{
			// VkTransformMatrixKHR uses a row-major memory layout, while glm::mat4
			// uses a column-major memory layout. We transpose the matrix so we can
			// memcpy the matrix's data directly.
			glm::mat4 temp = glm::transpose(matrix);
			VkTransformMatrixKHR out_matrix;
			memcpy(&out_matrix, &temp, sizeof(VkTransformMatrixKHR));
			return out_matrix;
		}

		VkDeviceAddress GetBufferDeviceAddress(VkBuffer& buffer)
		{
			VkBufferDeviceAddressInfo info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, buffer };
			return vkGetBufferDeviceAddress(RendererContext::GetCurrentVulkanDevice(), &info);
		}

	}

	AccelerationStructureBuilder::AccelerationStructureBuilder()
		: m_ScratchAlignment(RendererContext::GetCurrentDevice()->GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment)
	{
		auto properties = RendererContext::GetCurrentDevice()->GetAccelerationStructureProperties();
		m_CommandBuffer = CreateScope<CommandBuffer>(Renderer::GetRendererConfig().FramesInFlight, "Acceleration Structure Builder");
		
	}

	AccelerationStructureBuilder::~AccelerationStructureBuilder()
	{
	}

	void AccelerationStructureBuilder::BeginTLASBuild(VkBuildAccelerationStructureFlagsKHR flags, bool update /* = false */, bool motion /* = false */)
	{
		// Cannot call buildTlas twice except to update.
		CORE_ASSERT(m_TLAS == nullptr || update, "");
		// Cannot call buildTlas twice except to update.
		m_Flags = flags;
		m_UpdateTLAS = update;
		m_MotionTLAS = motion;
	}

	void AccelerationStructureBuilder::SubmitMeshInstance(const Ref<Mesh>& mesh, const glm::mat4& transform)
	{
		VkTransformMatrixKHR transformKRH = utils::TransformFromGLMToKHRMatrix(transform);
		static uint32_t meshInstanceIndex = 0; // TODO: sort this out
		for (Ref<BottomLevelAccelerationStructure> blas : mesh->GetMeshSource()->GetBLAccelerationStructure())
		{
			VkAccelerationStructureInstanceKHR intance{};
			intance.transform = transformKRH;											// Position of the instance
			intance.instanceCustomIndex = meshInstanceIndex;							// gl_InstanceCustomIndexEXT
			intance.accelerationStructureReference = blas->GetDeviceAddress();
			intance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
			intance.mask = 0xFF;       //  Only be hit if rayMask & instance.mask != 0
			intance.instanceShaderBindingTableRecordOffset = 0;  // We will use the same hit group for all objects
			m_TLASInstances.emplace_back(intance);
			meshInstanceIndex++;
		}
	}

	void AccelerationStructureBuilder::EndTLASBuild()
	{
		uint32_t countInstance = static_cast<uint32_t>(m_TLASInstances.size());
		// Command buffer to create the TLAS
		VkCommandBuffer   cmdBuf = Renderer::GetCommandBuffer(true, false);

		ResourceAllocator allocator("Top Level Acceleration Structure");
		
		VkBuffer instancesBuffer;
		VmaAllocation instBufferAllocation = CreateInstanceBuffer(m_TLASInstances, allocator, instancesBuffer);  // Buffer of instances containing the matrices and BLAS ids
		VkDeviceAddress           instBufferAddr = utils::GetBufferDeviceAddress(instancesBuffer);
		/**/
		// Make sure the copy of the instance buffer are copied before triggering the acceleration structure build
		VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			0, 1, &barrier, 0, nullptr, 0, nullptr);

		/**/
		// Creating the TLAS
		VkBuffer scratchBuffer;
		VmaAllocation scratchBufferAllocation = CreateTlas(cmdBuf, countInstance, instBufferAddr, m_Flags, false, false, allocator, scratchBuffer);

		// Finalizing and destroying temporary data
		Renderer::FlushCommandBuffer(cmdBuf, false, true);

		allocator.DestroyBuffer(instancesBuffer, instBufferAllocation);
		allocator.DestroyBuffer(scratchBuffer, scratchBufferAllocation);

	}

	void AccelerationStructureBuilder::BeginBLASBuild(VkBuildAccelerationStructureFlagsKHR flags)
	{
		m_Flags = flags;
	}

	void AccelerationStructureBuilder::SummitMesh(const Ref<Mesh>& mesh)
	{
		// Convert Mesh geometry to VkGeometryKHR
		// BLAS builder requires raw device addresses.
		VkBuffer vertexBuffer = mesh->GetMeshSource()->GetVertexBuffer()->GetVulkanBuffer();
		VkDeviceAddress vertexAddress = utils::GetBufferDeviceAddress(vertexBuffer);

		VkBuffer indexBuffer = mesh->GetMeshSource()->GetIndexBuffer()->GetVulkanBuffer();
		VkDeviceAddress indexAddress = utils::GetBufferDeviceAddress(indexBuffer);

		uint32_t maxPrimitiveCount = mesh->GetMeshSource()->GetIndexBuffer()->GetCount() / 3;
		uint32_t vertexBufferStride = mesh->GetMeshSource()->GetVertexBufferLayout().GetStride();
		// Describe buffer as array of VertexElement.
		VkAccelerationStructureGeometryTrianglesDataKHR triangles{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
		triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;  // vec3 vertex position data.
		triangles.vertexData.deviceAddress = vertexAddress;
		triangles.vertexStride = vertexBufferStride;
		triangles.maxVertex = mesh->GetMeshSource()->GetVertexBuffer()->GetSize() / vertexBufferStride;
		// Describe index data (32-bit unsigned int)
		triangles.indexType = VK_INDEX_TYPE_UINT32;
		triangles.indexData.deviceAddress = indexAddress;
		// Indicate identity transform by setting transformData to null device pointer.
		triangles.transformData = {};

		BLAStructureInputData blas;
		// Identify the above data as containing opaque triangles.
		// NOTE: Our blas is made from only one geometry, but could be made of many geometries
		blas.ASGeometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		blas.ASGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		blas.ASGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		blas.ASGeometry.geometry.triangles = triangles;

		// The entire array will be used to build the BLAS.
		blas.ASBuildOffsetInfo.firstVertex = 0;
		blas.ASBuildOffsetInfo.primitiveCount = maxPrimitiveCount;
		blas.ASBuildOffsetInfo.primitiveOffset = 0;
		blas.ASBuildOffsetInfo.transformOffset = 0;
		// Add the blas to the list of all blas
		blas.MeshSourceHandle = mesh->GetMeshSource()->Handle;
		m_BLASInputData.push_back(blas);
	}

	void AccelerationStructureBuilder::EndBLASBuild(std::vector<Ref<BottomLevelAccelerationStructure>>& outBlas)
	{
		VkDevice device = RendererContext::GetCurrentVulkanDevice();
		uint32_t         nbBlas = static_cast<uint32_t>(m_BLASInputData.size());
		VkDeviceSize asTotalSize{ 0 };     // Memory size of all allocated BLAS
		uint32_t     nbCompactions{ 0 };   // Nb of BLAS requesting compaction
		VkDeviceSize maxScratchSize{ 0 };  // Largest scratch size

		// Preparing the information for the acceleration build commands.
		std::vector<BuildAccelerationStructure> buildAs(nbBlas);
		for (uint32_t idx = 0; idx < nbBlas; idx++)
		{
			// Filling partially the VkAccelerationStructureBuildGeometryInfoKHR for querying the build sizes.
			// Other information will be filled in the createBlas (see #2)
			buildAs[idx].BuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			buildAs[idx].BuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			buildAs[idx].BuildInfo.flags = m_Flags;
			buildAs[idx].BuildInfo.geometryCount = 1;
			buildAs[idx].BuildInfo.pGeometries = &(m_BLASInputData[idx].ASGeometry);

			// Build range information
			buildAs[idx].RangeInfo = &(m_BLASInputData[idx].ASBuildOffsetInfo);

			// Finding sizes to create acceleration structures and scratch
			uint32_t maxPrimCount = m_BLASInputData[idx].ASBuildOffsetInfo.primitiveCount; // Number of primitives/triangles

			ext::vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
				&buildAs[idx].BuildInfo, &maxPrimCount, &buildAs[idx].SizeInfo);

			buildAs[idx].SizeInfo.buildScratchSize = utils::RoundUp(buildAs[idx].SizeInfo.buildScratchSize, m_ScratchAlignment);
			// Extra info
			asTotalSize += buildAs[idx].SizeInfo.accelerationStructureSize;
			maxScratchSize = std::max(maxScratchSize, buildAs[idx].SizeInfo.buildScratchSize);
			nbCompactions += HasFlag(buildAs[idx].BuildInfo.flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
		}

		// Allocate the scratch buffers holding the temporary data of the acceleration structure builder
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufferInfo.size = maxScratchSize;

		VkBuffer scratchBuffer;
		ResourceAllocator allocator("ScratchBuffers");
		VmaAllocation scratchBufferAllocation =  allocator.AllocateBuffer(bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY, VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, scratchBuffer);
		

		VkDeviceAddress scratchBufferAddress = utils::GetBufferDeviceAddress(scratchBuffer);


		// Allocate a query pool for storing the needed size for every BLAS compaction.
		VkQueryPool queryPool{ VK_NULL_HANDLE };
		if (nbCompactions > 0)  // Is compaction requested?
		{
			assert(nbCompactions == nbBlas);  // Don't allow mix of on/off compaction
			VkQueryPoolCreateInfo qpci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			qpci.queryCount = nbBlas;
			qpci.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
			vkCreateQueryPool(device, &qpci, nullptr, &queryPool);
		}

		// Batching creation/compaction of BLAS to allow staying in restricted amount of memory
		std::vector<uint32_t> indices;  // Indices of the BLAS to create
		VkDeviceSize          batchSize{ 0 };
		VkDeviceSize          batchLimit{ 256'000'000 };  // 256 MB

		for (uint32_t idx = 0; idx < nbBlas; idx++)
		{
			indices.push_back(idx);
			batchSize += buildAs[idx].SizeInfo.accelerationStructureSize;
			
			// Over the limit or last BLAS element
			if (batchSize >= batchLimit || idx == nbBlas - 1)
			{
				MeshHandle meshSourceHandle = m_BLASInputData[idx].MeshSourceHandle;
				m_CommandBuffer->RT_Begin();
				VkCommandBuffer cmdBuf = m_CommandBuffer->GetCommandBuffer();
				CreateBlas(cmdBuf, indices, buildAs, scratchBufferAddress, queryPool, meshSourceHandle);
				m_CommandBuffer->RT_End();
				m_CommandBuffer->RT_Submit();
				VkQueue graphicsQueue = RendererContext::GetCurrentDevice()->GetGraphicsQueue();
				VK_CHECK_RESULT(vkQueueWaitIdle(graphicsQueue));

				if (queryPool)
				{
					// After creation, the real space can be smaller, and it is possible to copy the acceleration structure 
					// to one that is using exactly what is needed. This could save over 50% of the device memory usage
					m_CommandBuffer->RT_Begin();
					CompactBlas(cmdBuf, indices, buildAs, queryPool);
					m_CommandBuffer->RT_End();
					m_CommandBuffer->RT_Submit();
					VK_CHECK_RESULT(vkQueueWaitIdle(graphicsQueue));
					// Destroy the non-compacted version
					DestroyNonCompacted(indices, buildAs);
				}
				// Reset
				batchSize = 0;
				indices.clear();
				// TODO: removed? currently we are using one single scratch buffer to build all the blas
				/** /
				scratchBufferAddress += buildAs[idx].SizeInfo.buildScratchSize;
				/**/
			}
		}

		// Logging reduction
		if (queryPool)
		{
			VkDeviceSize compactSize = 0;
			for (uint32_t i = 0; i < buildAs.size(); i++)
				compactSize += buildAs[i].SizeInfo.accelerationStructureSize;
		
			/**/
			AS_LOG_INFO(" Ray Tracing BLAS: reducing from: {0} to: {1} => Reduction of {2} ({3}% smaller) \n", asTotalSize, compactSize,
				asTotalSize - compactSize, (asTotalSize - compactSize) / float(asTotalSize) * 100.f);
			/**/
		}

		// Keeping all the created acceleration structures
		for (auto& b : buildAs)
		{
			m_BLAS.emplace_back(b.AStructure);
		}

		// Clean up
		vkDestroyQueryPool(device, queryPool, nullptr);
		allocator.DestroyBuffer(scratchBuffer, scratchBufferAllocation);
		m_Flags = 0;
		m_BLASInputData.clear();
		//buildAs.clear();
		outBlas = m_BLAS;
	}

	VmaAllocation AccelerationStructureBuilder::CreateInstanceBuffer(const std::vector<VkAccelerationStructureInstanceKHR>& ASInstances, ResourceAllocator& allocator, VkBuffer& outIntanceBuffer)
	{
		uint32_t bufferSize = ASInstances.size() * sizeof(ASInstances[0]);
		// CREATE STAGING BUFFER
		// Information to create a buffer (doesn't include assigning memory)
		VkBufferCreateInfo stagingBufferCreateInfo = {};
		stagingBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		stagingBufferCreateInfo.size = bufferSize;									// Size of buffer (size of 1 vertex * number of vertices)
		stagingBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;			// Multiple types of buffer possible, we want Transfer Buffer 
		// to sent the data to the Vertex buffer on the GPU
		stagingBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;			// Similar to Swap Chain images, can share vertex buffers

		VkBuffer stagingBuffer;
		// Allocation is the piece of memory associate with the buffer
		VmaAllocation stagingBufferAllocation = allocator.AllocateBuffer(stagingBufferCreateInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer);

		// Copy data to staging buffer
		// 1. Create pointer to a point in normal memory and  "Map" the staging buffer memory to that point
		uint8_t* destData = allocator.MapMemory<uint8_t>(stagingBufferAllocation);
		// 2. Copy memory from vertices vector to the point
		memcpy(destData, ASInstances.data(), bufferSize);
		// 3. Unmap the vertex buffer memory
		allocator.UnmapMemory(stagingBufferAllocation);

		// CREATE INSTANCE BUFFER
		// Create a buffer holding the actual instance data (matrices++) for use by the AS builder
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufferInfo.size = bufferSize;
		// Create Intance Buffer
		VmaAllocation instanceAllocation = allocator.AllocateBuffer(bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY, outIntanceBuffer);
		// Get a copy command buffer to tranfer the data 
		// from the staging buffer to the vertex buffer
		VkCommandBuffer copyCmd = Renderer::GetCommandBuffer(true);

		// Region of data to copy from and to
		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = 0;			// Copy from the begining of the fisrt buffer
		copyRegion.dstOffset = 0;			// Copy from the begining of the destination buffer
		copyRegion.size = bufferSize;
		// Record copy operation to the command buffer
		vkCmdCopyBuffer(copyCmd, stagingBuffer, outIntanceBuffer, 1, &copyRegion);
		// Flush command bufer (sumit to execution and detroy)
		Renderer::FlushCommandBuffer(copyCmd);

		// Destroy staging buffer, not longer need it
		allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);
		return instanceAllocation;
	}

	void AccelerationStructureBuilder::DestroyNonCompacted(std::vector<uint32_t> indices, std::vector<BuildAccelerationStructure>& buildAs)
	{
		for (auto& i : indices)
		{
			buildAs[i].CleanupAStructure->Release();
			buildAs[i].CleanupAStructure = nullptr;
		}
	}

	void AccelerationStructureBuilder::CompactBlas(VkCommandBuffer cmdBuf, std::vector<uint32_t> indices, std::vector<BuildAccelerationStructure>& buildAs, VkQueryPool queryPool)
	{
		VkDevice device = RendererContext::GetCurrentVulkanDevice();
		uint32_t queryCtn{ 0 };

		// Get the compacted size result back
		std::vector<VkDeviceSize> compactSizes(static_cast<uint32_t>(indices.size()));
		VK_CHECK_RESULT(vkGetQueryPoolResults(device, queryPool, 0, (uint32_t)compactSizes.size(), compactSizes.size() * sizeof(VkDeviceSize),
		compactSizes.data(), sizeof(VkDeviceSize), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

		for (auto idx : indices)
		{
			buildAs[idx].CleanupAStructure = buildAs[idx].AStructure;					 // previous AS to destroy
			buildAs[idx].SizeInfo.accelerationStructureSize = compactSizes[queryCtn++];  // new reduced size
			MeshHandle meshSourceHandle = buildAs[idx].CleanupAStructure->GetMeshSourceHandle();
			// Creating a compact version of the AS
			buildAs[idx].AStructure = CreateRef<BottomLevelAccelerationStructure>(AccelerationStructureType::BottomLevel,
				buildAs[idx].SizeInfo.accelerationStructureSize, meshSourceHandle);

			// Copy the original BLAS to a compact version
			VkCopyAccelerationStructureInfoKHR copyInfo{ VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
			copyInfo.src = buildAs[idx].BuildInfo.dstAccelerationStructure;
			copyInfo.dst = buildAs[idx].AStructure->GetVulkanAccelerationStructure();
			copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
			ext::vkCmdCopyAccelerationStructureKHR(cmdBuf, &copyInfo);
		}
	}

	void AccelerationStructureBuilder::CreateBlas(VkCommandBuffer cmdBuf, std::vector<uint32_t> indices, std::vector<BuildAccelerationStructure>& buildAs, VkDeviceAddress scratchAddress, VkQueryPool queryPool, MeshHandle meshSourceHandle)
	{
		VkDevice device = RendererContext::GetCurrentVulkanDevice();
		// For querying the compaction size
		if (queryPool)  
			vkResetQueryPool(device, queryPool, 0, static_cast<uint32_t>(indices.size()));
		uint32_t queryCnt{ 0 };

		

		for (const auto& idx : indices)
		{
			
			// Actual allocation of buffer and acceleration structure.
			buildAs[idx].AStructure = CreateRef<BottomLevelAccelerationStructure>(AccelerationStructureType::BottomLevel, 
																		buildAs[idx].SizeInfo.accelerationStructureSize, meshSourceHandle);

			// BuildInfo #2 part
			// -- Setting where the build lands
			buildAs[idx].BuildInfo.dstAccelerationStructure = buildAs[idx].AStructure->GetVulkanAccelerationStructure();  
			// -- All build are using the same scratch buffer
			buildAs[idx].BuildInfo.scratchData.deviceAddress = scratchAddress; 

			// Building the bottom-level-acceleration-structure
			ext::vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildAs[idx].BuildInfo, &buildAs[idx].RangeInfo);

			// Since the scratch buffer is reused across builds, we need a barrier to ensure one build
			// is finished before starting the next one.
			VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
				VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);

			if (queryPool)
			{
				// Add a query to find the 'real' amount of memory needed, use for compaction
				ext::vkCmdWriteAccelerationStructuresPropertiesKHR(cmdBuf, 1, &buildAs[idx].BuildInfo.dstAccelerationStructure,
					VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queryPool, queryCnt++);
			}
		}
	}

	//--------------------------------------------------------------------------------------------------
	// Low level of Tlas creation
	//
	VmaAllocation AccelerationStructureBuilder::CreateTlas(VkCommandBuffer cmdBuf, uint32_t countInstance, VkDeviceAddress instBufferAddr, VkBuildAccelerationStructureFlagsKHR flags, bool update, bool motion, ResourceAllocator& allocator, VkBuffer& outScratchBuffer)
	{
		VkDevice device = RendererContext::GetCurrentVulkanDevice();
		// Wraps a device pointer to the above uploaded instances.
		VkAccelerationStructureGeometryInstancesDataKHR instancesVk{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
		instancesVk.data.deviceAddress = instBufferAddr;

		// Put the above into a VkAccelerationStructureGeometryKHR. We need to put the instances struct in a union and label it as instance data.
		VkAccelerationStructureGeometryKHR topASGeometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		topASGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		topASGeometry.geometry.instances = instancesVk;
		topASGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

		// Find sizes
		VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		buildInfo.flags = flags;
		buildInfo.geometryCount = 1;
		buildInfo.pGeometries = &topASGeometry;
		buildInfo.mode = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;

		// Query both the size of the finished acceleration structure and the amount of scratch memory needed.
		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		ext::vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo,
			&countInstance, &sizeInfo);

		const auto& acProperties = RendererContext::GetCurrentDevice()->GetAccelerationStructureProperties();

		// AccelerationStructure offset needs to be 256 bytes aligned (official Vulkan specs, don't ask me why).
		const uint64_t AccelerationStructureAlignment = 256;
		const uint64_t ScratchAlignment = acProperties.minAccelerationStructureScratchOffsetAlignment;

		sizeInfo.accelerationStructureSize = RoundUp(sizeInfo.accelerationStructureSize, AccelerationStructureAlignment);
		sizeInfo.buildScratchSize = RoundUp(sizeInfo.buildScratchSize, ScratchAlignment);
		
		// Create TLAS
		if (update == false || m_TLAS == nullptr)
			m_TLAS = CreateRef<TopLevelAccelerationStructure>(AccelerationStructureType::TopLevel, sizeInfo.accelerationStructureSize, countInstance, motion);
		
		// Allocate the scratch memory
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
		bufferInfo.size = sizeInfo.buildScratchSize;

		VmaAllocation scratchBufferAllocation = allocator.AllocateBuffer(bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY, outScratchBuffer);
		VkDeviceAddress           scratchAddress = utils::GetBufferDeviceAddress(outScratchBuffer);

		VkAccelerationStructureKHR vulkanTLAS = m_TLAS->GetVulkanAccelerationStructure();
		// Update build information
		buildInfo.srcAccelerationStructure = update ? vulkanTLAS : VK_NULL_HANDLE;
		buildInfo.dstAccelerationStructure = vulkanTLAS;
		buildInfo.scratchData.deviceAddress = scratchAddress;

		// Build Offsets info: n instances
		VkAccelerationStructureBuildRangeInfoKHR        buildOffsetInfo{ countInstance, 0, 0, 0 };
		const VkAccelerationStructureBuildRangeInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;

		// Build the TLAS
		ext::vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildInfo, &pBuildOffsetInfo);
	
		return scratchBufferAllocation;
	}



	VkDeviceAddress AccelerationStructure::GetDeviceAddress() const
	{
		VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		addressInfo.accelerationStructure = m_AStructure;
		return ext::vkGetAccelerationStructureDeviceAddressKHR(RendererContext::GetCurrentVulkanDevice(), &addressInfo);
	}

	AccelerationStructure::AccelerationStructure(AccelerationStructureType type, uint32_t size)
		: m_ASType(type)
	{
		m_Buffer.Size = size;
		// Actual allocation of buffer and acceleration structure.
		VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		createInfo.type = utils::CerberusASTypeToVulkanASType(m_ASType);
		createInfo.size = size;  // Will be used to allocate memory.

		// Allocating the buffer to hold the acceleration structure
		// Allocate the scratch buffers holding the temporary data of the acceleration structure builder
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		bufferInfo.size = size;

		ResourceAllocator allocator("ASBuffer");
		m_Buffer.Allocation = allocator.AllocateBuffer(bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY, m_Buffer.Buffer);
		createInfo.buffer = m_Buffer.Buffer;

		// Create the acceleration structure
		ext::vkCreateAccelerationStructureKHR(RendererContext::GetCurrentVulkanDevice(), &createInfo, nullptr, &m_AStructure);
	}

	BottomLevelAccelerationStructure::BottomLevelAccelerationStructure(AccelerationStructureType type, uint32_t size, MeshHandle sourceHandle)
		: AccelerationStructure(type, size), m_MeshSourceHandle(sourceHandle)
	{

	}

	BottomLevelAccelerationStructure::~BottomLevelAccelerationStructure()
	{
		if (m_AStructure)
			Release();
	}

	void BottomLevelAccelerationStructure::Release()
	{
		VkDevice device = RendererContext::GetCurrentVulkanDevice();
		ResourceAllocator allocator("ASBuffer");
		allocator.DestroyBuffer(m_Buffer.Buffer, m_Buffer.Allocation);
		ext::vkDestroyAccelerationStructureKHR(device, m_AStructure, nullptr);
		m_AStructure = nullptr;
	}

	TopLevelAccelerationStructure::TopLevelAccelerationStructure(AccelerationStructureType type, uint32_t size, int32_t countInstance, bool motion)
	{
		m_Buffer.Size = size;
		m_ASType = type;
		VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		createInfo.size = size;
		/**/
#ifdef VK_NV_ray_tracing_motion_blur
		if (motion)
		{
			VkAccelerationStructureMotionInfoNV motionInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MOTION_INFO_NV };
			motionInfo.maxInstances = countInstance;
			createInfo.createFlags = VK_ACCELERATION_STRUCTURE_CREATE_MOTION_BIT_NV;
			createInfo.pNext = &motionInfo;
		}
#endif
		/**/
		// Allocating the buffer to hold the acceleration structure
		// Allocate the scratch buffers holding the temporary data of the acceleration structure builder
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
		bufferInfo.size = size;

		ResourceAllocator allocator("ASBuffer");
		m_Buffer.Allocation = allocator.AllocateBuffer(bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY, m_Buffer.Buffer);
		createInfo.buffer = m_Buffer.Buffer;

		// Create the acceleration structure
		ext::vkCreateAccelerationStructureKHR(RendererContext::GetCurrentVulkanDevice(), &createInfo, nullptr, &m_AStructure);


	}

	TopLevelAccelerationStructure::~TopLevelAccelerationStructure()
	{

	}
	void TopLevelAccelerationStructure::Release()
	{

	}
}