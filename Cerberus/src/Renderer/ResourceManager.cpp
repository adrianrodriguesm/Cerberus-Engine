#include "pch.h"
#include "ResourceManager.h"
#include "ResourceAllocator.h"
#include "Core/Pool.h"
namespace cerberus
{
	struct ResourceManagerData
	{
		// TODO: Weak ref intead!?
		/** /
		std::vector<AccelerationStructure*> AccelerationStructures;
		std::vector<GPUBuffer*> GPUBuffers;
		std::vector<MeshSourceProxy*> MeshSourceProxy;
		std::vector<MeshInstanceProxy*> MeshInstanceProxy;
		/**/
		Pool<GPUBuffer> GPUBuffers;
		Pool<AccelerationStructure> AccelerationStructures;
		Pool<MeshSourceProxy> MeshSourceProxy;
		Pool<MeshInstanceProxy> MeshInstanceProxy;

	};

	static ResourceManagerData* s_RMData;

	void ResourceManager::Init()
	{
		s_RMData = new ResourceManagerData();
	}
	void ResourceManager::Shutdown()
	{
		s_RMData->GPUBuffers.Clear();
		s_RMData->AccelerationStructures.Clear();
		s_RMData->MeshSourceProxy.Clear();
		s_RMData->MeshInstanceProxy.Clear();

		delete s_RMData;

#if 0
		// AS
		for (AccelerationStructure* as : s_RMData.AccelerationStructures)
		{
			if(as)
				delete as;
		}
		s_RMData.AccelerationStructures.clear();
		// GPUBuffers
		for (GPUBuffer* buffer : s_RMData.GPUBuffers)
			delete buffer;
		s_RMData.GPUBuffers.clear();
		// MeshSourceProxy
		for (MeshSourceProxy* meshSource : s_RMData.MeshSourceProxy)
			delete meshSource;
		s_RMData.MeshSourceProxy.clear();
		// MeshInstanceProxy
		for (MeshInstanceProxy* meshInstance: s_RMData.MeshInstanceProxy)
			delete meshInstance;
		s_RMData.MeshInstanceProxy.clear();
#endif

	}

	Handle<AccelerationStructure> ResourceManager::CreateAccelerationStructure(const AccelerationStructureSpecification& asSpecification)
	{
#if 0
		int16_t index = (int16_t)s_RMData.AccelerationStructures.size();
		UUID_16 generationID = UUID_16();
		Handle<AccelerationStructure> asHandle = Handle<AccelerationStructure>(generationID, index);

		AccelerationStructure* as = new AccelerationStructure(asSpecification);
		s_RMData.AccelerationStructures.push_back(as);

		return asHandle;
#endif
		return s_RMData->AccelerationStructures.Emplace(asSpecification);
	}

	AccelerationStructure* ResourceManager::GetAccelerationStructure(const Handle<AccelerationStructure>& asHandle)
	{
#if 0
		CORE_ASSERT(asHandle.IsValid(), "Acceleration Structure handle is not valid");
		return s_RMData.AccelerationStructures[asHandle.m_Index];
#endif
		return s_RMData->AccelerationStructures.Get(asHandle);
	}
	void ResourceManager::DestroyAccelerationStructure(Handle<AccelerationStructure>& asHandle)
	{
#if 0
		CORE_ASSERT(asHandle.IsValid(), "Acceleration Structure handle is not valid");

		AccelerationStructure* as = s_RMData.AccelerationStructures[asHandle.m_Index];
		if (as)
			delete as;

		s_RMData.AccelerationStructures[asHandle.m_Index] = nullptr;
		//s_RMData.AccelerationStructures.erase(s_RMData.AccelerationStructures.begin() + asHandle.m_Index);
		asHandle.Invalidate();
#endif
		s_RMData->AccelerationStructures.Remove(asHandle);
	}

	Handle<GPUBuffer> ResourceManager::CreateBuffer(const GPUBufferSpecification& gpuBufferSpec)
	{
#if 0
		int16_t index = (int16_t)s_RMData.GPUBuffers.size();
		UUID_16 generationID = UUID_16();
		Handle<GPUBuffer> bufferHandle = Handle<GPUBuffer>(generationID, index);

		// Information to create a buffer (doesn't include assigning memory)
		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = gpuBufferSpec.Size;											// Size of buffer (size of 1 vertex * number of vertices)
		bufferCreateInfo.usage = utils::CerberusToVulkanBufferUsage(gpuBufferSpec.Usage);	// Multiple types of buffer possible, we want Transfer Buffer 
		// to sent the data to the Vertex buffer on the GPU
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;							// Similar to Swap Chain images, can share vertex buffers

		// Allocate and create the actual buffer
		GPUBuffer* buffer = new GPUBuffer();
		ResourceAllocator allocator(gpuBufferSpec.DebugName);
		buffer->Allocation = allocator.AllocateBuffer(bufferCreateInfo, gpuBufferSpec.MemoryUsage, buffer->Buffer);
		//buffer->DebugName = gpuBufferSpec.DebugName;
		buffer->Size = gpuBufferSpec.Size;

		s_RMData.GPUBuffers.push_back(buffer);

		return bufferHandle;
#endif
		return s_RMData->GPUBuffers.Emplace(gpuBufferSpec);
	}

	void ResourceManager::DestroyBuffer(Handle<GPUBuffer>& bufferHandle)
	{
#if 0
		CORE_ASSERT(bufferHandle.IsValid(), "GPUBuffer handle is not valid");

		GPUBuffer* buffer = s_RMData.GPUBuffers[bufferHandle.m_Index];
		if (buffer)
		{
			ResourceAllocator allocator("");
			allocator.DestroyBuffer(buffer->Buffer, buffer->Allocation);
			delete buffer;
		}

		//s_RMData.GPUBuffers.erase(s_RMData.GPUBuffers.begin() + bufferHandle.m_Index);
		bufferHandle.Invalidate();

#endif // 0
		s_RMData->GPUBuffers.Remove(bufferHandle);
	}

	GPUBuffer* ResourceManager::GetBuffer(const Handle<GPUBuffer>& buffer)
	{
#if 0
		CORE_ASSERT(buffer.IsValid(), "GPUBuffer handle is not valid");
		return s_RMData.GPUBuffers[buffer.m_Index];
#endif
		return s_RMData->GPUBuffers.Get(buffer);
	}

	Handle<MeshSourceProxy> ResourceManager::CreateMeshSourceProxy(const void* vertexData, uint32_t vertexSize, const void* indexData, uint32_t indexSize)
	{
#if 0
		int16_t index = (int16_t)s_RMData.MeshSourceProxy.size();
		UUID_16 generationID = UUID_16();
		Handle<MeshSourceProxy> meshSourceProxyHandle = Handle<MeshSourceProxy>(generationID, index);

		MeshSourceProxy* meshSourceProxy = new MeshSourceProxy(vertexData, vertexSize, indexData, indexSize);
		s_RMData.MeshSourceProxy.push_back(meshSourceProxy);

		return meshSourceProxyHandle;
#endif
		return s_RMData->MeshSourceProxy.Emplace(vertexData, vertexSize, indexData, indexSize);
	}

	void ResourceManager::DestroyMeshSourceProxy(Handle<MeshSourceProxy>& meshSourceProxyHandle)
	{
#if 0
		CORE_ASSERT(meshSourceProxyHandle.IsValid(), "Mesh source proxy handle is not valid");

		MeshSourceProxy* meshProxy = s_RMData.MeshSourceProxy[meshSourceProxyHandle.m_Index];
		if (meshProxy)
			delete meshProxy;

		s_RMData.MeshSourceProxy.erase(s_RMData.MeshSourceProxy.begin() + meshSourceProxyHandle.m_Index);
		meshSourceProxyHandle.Invalidate();
#endif // 0
		return s_RMData->MeshSourceProxy.Remove(meshSourceProxyHandle);

	}
	MeshSourceProxy* ResourceManager::GetMeshSourceProxy(const Handle<MeshSourceProxy>& meshSourceProxyHandle)
	{
#if 0
		CORE_ASSERT(meshSourceProxyHandle.IsValid(), "Mesh source proxy handle is not valid");
		return s_RMData.MeshSourceProxy[meshSourceProxyHandle.m_Index];
#endif // 0
		return s_RMData->MeshSourceProxy.Get(meshSourceProxyHandle);
	}

	Handle<MeshInstanceProxy> ResourceManager::CreateMeshInstanceProxy(Handle<MeshSourceProxy> meshSource, uint32_t submeshIndex, Ref<MaterialAsset> material)
	{
#if 0
		int16_t index = (int16_t)s_RMData.MeshInstanceProxy.size();
		UUID_16 generationID = UUID_16();
		Handle<MeshInstanceProxy> meshInstProxyHandle = Handle<MeshInstanceProxy>(generationID, index);

		MeshInstanceProxy* meshInstanceProxy = new MeshInstanceProxy();
		meshInstanceProxy->Material = material;
		meshInstanceProxy->MeshSource = meshSource;
		meshInstanceProxy->SubmeshIndex = submeshIndex;
		meshInstanceProxy->WorldTransform = math::MatFactory::CreateIdentityMat4();
		s_RMData.MeshInstanceProxy.push_back(meshInstanceProxy);

		return meshInstProxyHandle;
#endif // 0
		return s_RMData->MeshInstanceProxy.Emplace(meshSource, submeshIndex, material, math::MatFactory::CreateIdentityMat4());
	}
	void ResourceManager::DestroyMeshInstanceProxy(Handle<MeshInstanceProxy>& meshInstanceHandle)
	{
#if 0
		CORE_ASSERT(meshInstanceHandle.IsValid(), "Mesh instance proxy handle is not valid");

		MeshInstanceProxy* meshInsProxy = s_RMData.MeshInstanceProxy[meshInstanceHandle.m_Index];
		if (meshInsProxy)
			delete meshInsProxy;

		s_RMData.MeshInstanceProxy.erase(s_RMData.MeshInstanceProxy.begin() + meshInstanceHandle.m_Index);
		meshInstanceHandle.Invalidate();
#endif // 0

		s_RMData->MeshInstanceProxy.Remove(meshInstanceHandle);
	}
	MeshInstanceProxy* ResourceManager::GetMeshInstanceProxy(const Handle<MeshInstanceProxy>& meshInstanceHandle)
	{
#if 0
		CORE_ASSERT(meshInstanceHandle.IsValid(), "Mesh instance proxy handle is not valid");
		return s_RMData.MeshInstanceProxy[meshInstanceHandle.m_Index];
#endif // 0
		return s_RMData->MeshInstanceProxy.Get(meshInstanceHandle);
	}
}


