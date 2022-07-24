#include "pch.h"
#include "AccelerationStructure.h"
#include "Renderer/RendererContext.h"
#include "Renderer/Renderer.h"
#include "Renderer/VulkanAllocator.h"
#include "Renderer/VulkanExtensions.h"
namespace cerberus
{
	inline VkDeviceAddress GetBufferDeviceAddress(VkBuffer& buffer)
	{
		VkBufferDeviceAddressInfo info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, buffer };
		return vkGetBufferDeviceAddress(RendererContext::GetCurrentVulkanDevice(), &info);
	}

	AccelerationStructureBuilder::AccelerationStructureBuilder()
	{
		m_CommandBuffer = CreateScope<RenderCommandBuffer>(Renderer::GetRendererConfig().FramesInFlight, "Acceleration Structure Builder");
	}

	AccelerationStructureBuilder::~AccelerationStructureBuilder()
	{
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
		VkDeviceAddress vertexAddress = GetBufferDeviceAddress(vertexBuffer);

		VkBuffer indexBuffer = mesh->GetMeshSource()->GetIndexBuffer()->GetVulkanBuffer();
		VkDeviceAddress indexAddress = GetBufferDeviceAddress(indexBuffer);

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
		//triangles.transformData = {};

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
		m_BLASInputData.push_back(blas);
	}

	void AccelerationStructureBuilder::EndBLASBuild()
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

			extensions::vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
				&buildAs[idx].BuildInfo, &maxPrimCount, &buildAs[idx].SizeInfo);

			// Extra info
			asTotalSize += buildAs[idx].SizeInfo.accelerationStructureSize;
			maxScratchSize = std::max(maxScratchSize, buildAs[idx].SizeInfo.buildScratchSize);
			nbCompactions += HasFlag(buildAs[idx].BuildInfo.flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
		}

		// Allocate the scratch buffers holding the temporary data of the acceleration structure builder
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufferInfo.size = maxScratchSize;

		VkBuffer scratchBuffer;
		VulkanAllocator allocator("ScratchBuffers");
		VmaAllocation scratchBufferAllocation =  allocator.AllocateBuffer(bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY, VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, scratchBuffer);

		VkDeviceAddress scratchBufferAddress = GetBufferDeviceAddress(scratchBuffer);


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
				m_CommandBuffer->RT_Begin();
				VkCommandBuffer cmdBuf = Renderer::GetCommandBuffer(true);
				CreateBlas(cmdBuf, indices, buildAs, scratchBufferAddress, queryPool);
				m_CommandBuffer->RT_End();
				m_CommandBuffer->RT_Submit();

				if (queryPool)
				{
					m_CommandBuffer->RT_Begin();
					CompactBlas(cmdBuf, indices, buildAs, queryPool);
					m_CommandBuffer->RT_End();
					m_CommandBuffer->RT_Submit();  // Submit command buffer and call vkQueueWaitIdle

					// Destroy the non-compacted version
					DestroyNonCompacted(indices, buildAs);
				}
				// Reset
				batchSize = 0;
				indices.clear();
			}
		}

		// Logging reduction
		if (queryPool)
		{
			VkDeviceSize compactSize = 0;
			for (uint32_t i = 0; i < buildAs.size(); i++)
				compactSize += buildAs[i].SizeInfo.accelerationStructureSize;
		
			/**/
			NV_CORE_INFO(" RT BLAS: reducing from: {0} to: {1} = {2} ({3}% smaller) \n", asTotalSize, compactSize,
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
		vkGetQueryPoolResults(device, queryPool, 0, (uint32_t)compactSizes.size(), compactSizes.size() * sizeof(VkDeviceSize),
			compactSizes.data(), sizeof(VkDeviceSize), VK_QUERY_RESULT_WAIT_BIT);

		for (auto idx : indices)
		{
			buildAs[idx].CleanupAStructure = buildAs[idx].AStructure;					 // previous AS to destroy
			buildAs[idx].SizeInfo.accelerationStructureSize = compactSizes[queryCtn++];  // new reduced size

			// Creating a compact version of the AS
			buildAs[idx].AStructure = CreateRef<AccelerationStructure>(AccelerationStructureType::BottomLevel,
										buildAs[idx].SizeInfo.accelerationStructureSize);

			// Copy the original BLAS to a compact version
			VkCopyAccelerationStructureInfoKHR copyInfo{ VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
			copyInfo.src = buildAs[idx].BuildInfo.dstAccelerationStructure;
			copyInfo.dst = buildAs[idx].AStructure->GetVulkanAccelerationStructure();
			copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
			extensions::vkCmdCopyAccelerationStructureKHR(cmdBuf, &copyInfo);
		}
	}

	void AccelerationStructureBuilder::CreateBlas(VkCommandBuffer cmdBuf, std::vector<uint32_t> indices, std::vector<BuildAccelerationStructure>& buildAs, VkDeviceAddress scratchAddress, VkQueryPool queryPool)
	{
		VkDevice device = RendererContext::GetCurrentVulkanDevice();
		// For querying the compaction size
		if (queryPool)  
			vkResetQueryPool(device, queryPool, 0, static_cast<uint32_t>(indices.size()));
		uint32_t queryCnt{ 0 };

		for (const auto& idx : indices)
		{
			// Actual allocation of buffer and acceleration structure.
			buildAs[idx].AStructure = CreateRef<AccelerationStructure>(AccelerationStructureType::BottomLevel, 
																		buildAs[idx].SizeInfo.accelerationStructureSize);

			// BuildInfo #2 part
			// -- Setting where the build lands
			buildAs[idx].BuildInfo.dstAccelerationStructure = buildAs[idx].AStructure->GetVulkanAccelerationStructure();  
			// -- All build are using the same scratch buffer
			buildAs[idx].BuildInfo.scratchData.deviceAddress = scratchAddress;  

			// Building the bottom-level-acceleration-structure
			extensions::vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildAs[idx].BuildInfo, &buildAs[idx].RangeInfo);

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
				extensions::vkCmdWriteAccelerationStructuresPropertiesKHR(cmdBuf, 1, &buildAs[idx].BuildInfo.dstAccelerationStructure,
					VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queryPool, queryCnt++);
			}
		}
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
	}
	AccelerationStructure::AccelerationStructure(AccelerationStructureType type, uint32_t size)
		: m_ASType(type)
	{
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

		VulkanAllocator allocator("ASBuffer");
		m_Buffer.Allocation = allocator.AllocateBuffer(bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY, m_Buffer.Buffer);
		createInfo.buffer = m_Buffer.Buffer;

		// Create the acceleration structure
		extensions::vkCreateAccelerationStructureKHR(RendererContext::GetCurrentVulkanDevice(), &createInfo, nullptr, &m_AStructure);
	}

	AccelerationStructure::~AccelerationStructure()
	{
		Release();
	}
	void AccelerationStructure::Release()
	{
		VkDevice device = RendererContext::GetCurrentVulkanDevice();
		VulkanAllocator allocator("ASBuffer");
		allocator.DestroyBuffer(m_Buffer.Buffer, m_Buffer.Allocation);
		extensions::vkDestroyAccelerationStructureKHR(device, m_AStructure, nullptr);
	}
}