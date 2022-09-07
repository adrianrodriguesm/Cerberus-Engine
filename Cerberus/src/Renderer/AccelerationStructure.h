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
		VkDeviceAddress GetDeviceAddress() const;
		VkAccelerationStructureKHR GetVulkanAccelerationStructure() const { return m_AStructure; }
		virtual ~AccelerationStructure() = default;
	protected:
		AccelerationStructure() = default;
		AccelerationStructure(AccelerationStructureType type, uint32_t size);
	protected:
		AccelerationStructureType m_ASType;
		ASBuffer m_Buffer;
		VkAccelerationStructureKHR m_AStructure = VK_NULL_HANDLE;
	};

	class BottomLevelAccelerationStructure final : public AccelerationStructure
	{
	public:
		BottomLevelAccelerationStructure(AccelerationStructureType type, uint32_t size, MeshHandle sourceHandle);
		virtual ~BottomLevelAccelerationStructure();
		void Release();

		inline MeshHandle GetMeshSourceHandle() const { return m_MeshSourceHandle; }
	private:
		MeshHandle m_MeshSourceHandle;
	};

	class TopLevelAccelerationStructure final : public AccelerationStructure
	{
	public:
		TopLevelAccelerationStructure() = default;
		TopLevelAccelerationStructure(AccelerationStructureType type, uint32_t size, int32_t countInstance, bool motion);
		virtual ~TopLevelAccelerationStructure();
		void Release();

		
	private:
		uint32_t m_InstancesCount;
		VkAccelerationStructureGeometryInstancesDataKHR m_ASInstances{};
	};


	class AccelerationStructureBuilder final
	{
	public:
		struct BuildAccelerationStructure
		{
			VkAccelerationStructureBuildGeometryInfoKHR BuildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
			VkAccelerationStructureBuildSizesInfoKHR SizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
			const VkAccelerationStructureBuildRangeInfoKHR* RangeInfo;
			Ref<BottomLevelAccelerationStructure> AStructure;
			Ref<BottomLevelAccelerationStructure> CleanupAStructure;
		};
		struct BLAStructureInputData
		{
			// Inputs used to build Bottom-level acceleration structure.
			// You manage the lifetime of the buffer(s) referenced by the VkAccelerationStructureGeometryKHRs within.
			// In particular, you must make sure they are still valid and not being modified when the BLAS is built or updated.
			// Data used to build acceleration structure geometry
			VkAccelerationStructureGeometryKHR       ASGeometry;
			VkAccelerationStructureBuildRangeInfoKHR ASBuildOffsetInfo;
			MeshHandle MeshSourceHandle;
		};
	public:
		AccelerationStructureBuilder();
		~AccelerationStructureBuilder();

		// Creating the top-level acceleration structure from the vector of Instance
		 // - See struct of Instance
		 // - The resulting TLAS will be stored in m_tlas
		 // - update is to rebuild the Tlas with updated matrices
		void BeginTLASBuild(VkBuildAccelerationStructureFlagsKHR flags, bool update = false, bool motion = false);
		void SubmitMeshInstance(const Ref<Mesh>& mesh, glm::mat4& transform);
		void EndTLAS();
		//--------------------------------------------------------------------------------------------------
		// - There will be one BLAS per geometry (e.g mesh, submesh, etc) submission 
		// - The resulting BLAS (along with the inputs used to build) are stored in m_BLAS,
		//   and can be referenced by index.
		// - If flag has the 'Compact' flag, the BLAS will be compacted
		void BeginBLASBuild(VkBuildAccelerationStructureFlagsKHR flags);
		// Change to submesh
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
			VkQueryPool                              queryPool,
			MeshHandle								 meshHandle);
		//--------------------------------------------------------------------------------------------------
		// Creating the TLAS, called by buildTlas
		void CreateTlas(VkCommandBuffer                      cmdBuf,			// Command buffer
			uint32_t                             countInstance,					// number of instances
			VkDeviceAddress                      instBufferAddr,				// Buffer address of instances
			VkBuildAccelerationStructureFlagsKHR flags,							// Build creation flag
			bool                                 update,						// Update == animation
			bool                                 motion);						// Motion Blur
	private:
		Scope<RenderCommandBuffer> m_CommandBuffer;
		VkBuildAccelerationStructureFlagsKHR m_Flags;

		// Bottom Level Acceleration Structure related:
		uint32_t m_ScratchAlignment;
		std::vector<BLAStructureInputData> m_BLASInputData;
		std::vector<Ref<BottomLevelAccelerationStructure>> m_BLAS;
		// Top Level Acceleration Structure related:
		std::vector<VkAccelerationStructureInstanceKHR> m_TLASInstances;
		Ref<TopLevelAccelerationStructure> m_TLAS;
		bool m_UpdateTLAS = false, m_MotionTLAS = false;
	};
}