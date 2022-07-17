#pragma once
#include "Renderer/Mesh.h"
#include "Renderer/RenderCommandBuffer.h"
#include "Scene/Scene.h"
#include "Renderer/VulkanAllocator.h"
namespace cerberus
{
	enum class AccelerationStructureType
	{
		None = 0,
		TopLevel,
		BottomLevel = None
	};

	class AccelerationStructure
	{
	public:
		struct ASBuffer
		{
			VkBuffer Buffer;
			uint32_t Size;
			VmaAllocation Allocation;
		};
	public:
		AccelerationStructure(AccelerationStructureType type, uint32_t size);
		~AccelerationStructure();

		VkAccelerationStructureKHR GetVulkanAccelerationStructure() const { return m_AStructure; }

		void Release();

	private:
		AccelerationStructureType m_ASType;
		ASBuffer m_Buffer;
		VkAccelerationStructureKHR m_AStructure = VK_NULL_HANDLE;
	};




	class AccelerationStructureBuilder
	{
	public:
		struct BuildAccelerationStructure
		{
			VkAccelerationStructureBuildGeometryInfoKHR BuildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
			VkAccelerationStructureBuildSizesInfoKHR SizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
			const VkAccelerationStructureBuildRangeInfoKHR* RangeInfo;
			Ref<AccelerationStructure> AStructure;
			Ref<AccelerationStructure> CleanupAStructure;
		};
		struct BLAStructureInputData
		{
			// Inputs used to build Bottom-level acceleration structure.
			// You manage the lifetime of the buffer(s) referenced by the VkAccelerationStructureGeometryKHRs within.
			// In particular, you must make sure they are still valid and not being modified when the BLAS is built or updated.
			// Data used to build acceleration structure geometry
			VkAccelerationStructureGeometryKHR       ASGeometry;
			VkAccelerationStructureBuildRangeInfoKHR ASBuildOffsetInfo;
		};
	public:
		AccelerationStructureBuilder();
		~AccelerationStructureBuilder();

		//--------------------------------------------------------------------------------------------------
		// - There will be one BLAS per geometry (e.g mesh, submesh, etc) submission 
		// - The resulting BLAS (along with the inputs used to build) are stored in m_BLAS,
		//   and can be referenced by index.
		// - If flag has the 'Compact' flag, the BLAS will be compacted
		void BeginBLASBuild(VkBuildAccelerationStructureFlagsKHR flags);
		void SummitMesh(const Ref<Mesh>& mesh);
		void EndBLASBuild();

	private:
		bool HasFlag(VkFlags item, VkFlags flag) { return (item & flag) == flag; }
		//--------------------------------------------------------------------------------------------------
		// Destroy all the non-compacted acceleration structures
		void DestroyNonCompacted(std::vector<uint32_t> indices, std::vector<BuildAccelerationStructure>& buildAs);
		//--------------------------------------------------------------------------------------------------
		// Create and replace a new acceleration structure and buffer based on the size retrieved by the Query.
		void CompactBlas(VkCommandBuffer cmdBuf, std::vector<uint32_t> indices, std::vector<BuildAccelerationStructure>& buildAs, VkQueryPool queryPool);
		//--------------------------------------------------------------------------------------------------
		// Creating the bottom level acceleration structure for all indices of `buildAs` vector.
		// The array of BuildAccelerationStructure was created in buildBlas and the vector of
		// indices limits the number of BLAS to create at once. This limits the amount of
		// memory needed when compacting the BLAS.
		void CreateBlas(VkCommandBuffer              cmdBuf,
			std::vector<uint32_t>                    indices,
			std::vector<BuildAccelerationStructure>& buildAs,
			VkDeviceAddress                          scratchAddress,
			VkQueryPool                              queryPool);
	private:
		std::vector<BLAStructureInputData> m_BLASInputData;
		VkBuildAccelerationStructureFlagsKHR m_Flags;
		Scope<RenderCommandBuffer> m_CommandBuffer;

		std::vector<Ref<AccelerationStructure>> m_BLAS;
	};
}