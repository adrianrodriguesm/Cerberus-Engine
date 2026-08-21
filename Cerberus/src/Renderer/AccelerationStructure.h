#pragma once
#include "Renderer/Resources/MeshProxy.h"
#include "Renderer/GPUBuffer.h"
#include "Core/Handle.h"

#include <span>
namespace cerberus
{
	class RayTracingCommandList;

	enum class AccelerationStructureType
	{
		None = 0,
		TopLevel,
		BottomLevel = None
	};

	struct TLASInfo
	{
		bool Motion = true;
		uint32_t CountInstance = 0;
	};

	struct AccelerationStructureSpecification
	{
		AccelerationStructureType Type;
		uint32_t Size;
		TLASInfo* TLASInfo = nullptr;
	};

	class AccelerationStructure final
	{
	public:
		AccelerationStructure(const AccelerationStructureSpecification& asSpecification);
		AccelerationStructure(AccelerationStructure&& other) noexcept;
		AccelerationStructure(const AccelerationStructure&) = delete;
		AccelerationStructure& operator=(const AccelerationStructure&) = delete;
		AccelerationStructure& operator=(AccelerationStructure&&) = delete;
		~AccelerationStructure();

		VkDeviceAddress GetDeviceAddress() const;
		VkAccelerationStructureKHR GetVulkanAStructure() const { return m_AStructure; }
	private:
		AccelerationStructureType m_ASType;
		Handle<GPUBuffer> m_ASBuffer;
		VkAccelerationStructureKHR m_AStructure = VK_NULL_HANDLE;
	};

	class AccelerationStructureBuilder final
	{
	public:
		struct BuildAccelerationStructure
		{
			VkAccelerationStructureBuildGeometryInfoKHR BuildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
			VkAccelerationStructureBuildSizesInfoKHR SizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
			const VkAccelerationStructureBuildRangeInfoKHR* RangeInfo;
			Handle<AccelerationStructure> AStructure;
			Handle<AccelerationStructure> CleanupAStructure;
			Handle<MeshSourceProxy> MeshSourceProxy;
			uint32_t SubmeshIndex;
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

		// Creating the top-level acceleration structure from the vector of Instance
		 // - See struct of Instance
		 // - The resulting TLAS will be stored in m_tlas
		 // - update is to rebuild the Tlas with updated matrices
		void BeginTLASBuild(VkBuildAccelerationStructureFlagsKHR flags, bool motion = false);
		void SubmitMeshInstance(const Handle<MeshInstanceProxy>& meshInstance, uint32_t instanceCustomIndex);
		void EndTLASBuild(Handle<AccelerationStructure>& outTLAS);
		//--------------------------------------------------------------------------------------------------
		// - There will be one BLAS per geometry (e.g mesh, submesh, etc) submission 
		// - The resulting BLAS (along with the inputs used to build) are stored in m_BLAS,
		//   and can be referenced by index.
		// - If flag has the 'Compact' flag, the BLAS will be compacted
		void BeginBLASBuild(VkBuildAccelerationStructureFlagsKHR flags);
		// Change to submesh
		void SummitMesh(const Handle<MeshSourceProxy>& mesh);
		void EndBLASBuild();

	private:
		bool HasFlag(VkFlags item, VkFlags flag) { return (item & flag) == flag; }
		VmaAllocation CreateInstanceBuffer(std::span<const VkAccelerationStructureInstanceKHR> tlasInstances, ResourceAllocator& allocator, VkBuffer& outInstanceBuffer);
		//--------------------------------------------------------------------------------------------------
		// Destroy all the non-compacted acceleration structures
		void DestroyNonCompacted(std::span<const uint32_t> indices, std::span<BuildAccelerationStructure> buildAs);
		//--------------------------------------------------------------------------------------------------
		// Create and replace a new acceleration structure and buffer based on the size retrieved by the Query.
		void CompactBlas(const RayTracingCommandList& commandList, std::span<const uint32_t> indices, std::span<BuildAccelerationStructure> buildAs, VkQueryPool queryPool);
		//--------------------------------------------------------------------------------------------------
		// Creating the bottom level acceleration structure for all indices of `buildAs` vector.
		// The array of BuildAccelerationStructure was created in buildBlas and the vector of
		// indices limits the number of BLAS to create at once. This limits the amount of
		// memory needed when compacting the BLAS.
		void CreateBlas(const RayTracingCommandList& commandList,
			std::span<const uint32_t>                 indices,
			std::span<BuildAccelerationStructure>     buildAs,
			VkDeviceAddress                          scratchAddress,
			VkQueryPool                              queryPool);
		//--------------------------------------------------------------------------------------------------
		// Creating the TLAS, called by buildTlas
		VmaAllocation CreateTlas(const RayTracingCommandList& commandList,
			uint32_t                             countInstance,					// number of instances
			VkDeviceAddress                      instBufferAddr,				// Buffer address of instances
			VkBuildAccelerationStructureFlagsKHR flags,							// Build creation flag
			bool                                 update,						// Update == animation
			bool                                 motion,						// Motion Blur
			ResourceAllocator& allocator, 
			VkBuffer& outScratchBuffer,
			Handle<AccelerationStructure>& outTLAS);
	private:
		VkBuildAccelerationStructureFlagsKHR m_Flags;

		// Bottom Level Acceleration Structure related:
		uint32_t m_ScratchAlignment;
		std::unordered_map<Handle<MeshSourceProxy>, std::vector<BLAStructureInputData>> m_BLASInputData;
		uint32_t m_BLASCount;
		// Top Level Acceleration Structure related:
		std::vector<VkAccelerationStructureInstanceKHR> m_TLASInstances;
		bool m_UpdateTLAS = false, m_MotionTLAS = false;
	};
}
