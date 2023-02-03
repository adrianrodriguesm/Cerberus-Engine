#include "pch.h"
#include "ResourceManager.h"
#include "ResourceAllocator.h"
#include "Core/Pool.h"
#include "Renderer/Descriptor.h"
namespace cerberus
{
	struct ResourceManagerData
	{
		// TODO: Weak ref intead!?
		Pool<GPUBuffer> GPUBuffers;
		Pool<AccelerationStructure> AccelerationStructures;
		Pool<MeshSourceProxy> MeshSourceProxy;
		Pool<MeshInstanceProxy> MeshInstanceProxy;
		Pool<BindGroupLayout> BindGroupLayouts;
		Pool<BindGroup> BindGroup;
		Pool<Image> Images;
		Pool<ImageView> ImageViews;
		Pool<Texture2D> Textures2D;

		DescriptorAllocator* PersistentDescriptorAllocator;

		std::unordered_map<Handle<Image>, std::vector<Handle<ImageView>>> ImageRegister;
	};

	static ResourceManagerData* s_RMData;

	void ResourceManager::Init()
	{
		s_RMData = new ResourceManagerData();

		//////////////////////////////////
		// Create Descriptor Pool
		//////////////////////////////////
		DescriptorAllocatorSpecification descriptorAllocatorSpec;
		descriptorAllocatorSpec.NumberOfFrames = GetRendererConfig().FramesInFlight;
		descriptorAllocatorSpec.MaxSets = 1000;
		descriptorAllocatorSpec.IsPersistent = true;
		s_RMData->PersistentDescriptorAllocator = new DescriptorAllocator(descriptorAllocatorSpec);
	}
	void ResourceManager::Shutdown()
	{
		s_RMData->GPUBuffers.Clear();
		s_RMData->AccelerationStructures.Clear();
		s_RMData->MeshSourceProxy.Clear();
		s_RMData->MeshInstanceProxy.Clear();
		s_RMData->BindGroupLayouts.Clear();

		delete s_RMData->PersistentDescriptorAllocator;

		delete s_RMData;
	}

	Handle<AccelerationStructure> ResourceManager::CreateAccelerationStructure(const AccelerationStructureSpecification& asSpecification)
	{
		return s_RMData->AccelerationStructures.Emplace(asSpecification);
	}

	AccelerationStructure* ResourceManager::GetAccelerationStructure(const Handle<AccelerationStructure>& asHandle)
	{
		return s_RMData->AccelerationStructures.Get(asHandle);
	}
	void ResourceManager::DestroyAccelerationStructure(Handle<AccelerationStructure>& asHandle)
	{
		s_RMData->AccelerationStructures.Remove(asHandle);
	}

	Handle<GPUBuffer> ResourceManager::CreateBuffer(const GPUBufferSpecification& gpuBufferSpec)
	{
		return s_RMData->GPUBuffers.Emplace(gpuBufferSpec);
	}

	void ResourceManager::DestroyBuffer(const Handle<GPUBuffer>& bufferHandle)
	{
		s_RMData->GPUBuffers.Remove(bufferHandle);
	}

	GPUBuffer* ResourceManager::GetBuffer(const Handle<GPUBuffer>& buffer)
	{
		return s_RMData->GPUBuffers.Get(buffer);
	}

	Handle<MeshSourceProxy> ResourceManager::CreateMeshSourceProxy(const void* vertexData, uint32_t vertexSize, const void* indexData, uint32_t indexSize, const std::vector<Submesh>& submeshes)
	{
		return s_RMData->MeshSourceProxy.Emplace(vertexData, vertexSize, indexData, indexSize, submeshes);
	}

	void ResourceManager::DestroyMeshSourceProxy(Handle<MeshSourceProxy>& meshSourceProxyHandle)
	{
		return s_RMData->MeshSourceProxy.Remove(meshSourceProxyHandle);

	}
	MeshSourceProxy* ResourceManager::GetMeshSourceProxy(const Handle<MeshSourceProxy>& meshSourceProxyHandle)
	{
		return s_RMData->MeshSourceProxy.Get(meshSourceProxyHandle);
	}

	Handle<MeshInstanceProxy> ResourceManager::CreateMeshInstanceProxy(Handle<MeshSourceProxy> meshSource, uint32_t submeshIndex, Ref<MaterialAsset> material)
	{
		return s_RMData->MeshInstanceProxy.Emplace(meshSource, submeshIndex, material, math::MatFactory::CreateIdentityMat4());
	}
	void ResourceManager::DestroyMeshInstanceProxy(Handle<MeshInstanceProxy>& meshInstanceHandle)
	{
		s_RMData->MeshInstanceProxy.Remove(meshInstanceHandle);
	}
	MeshInstanceProxy* ResourceManager::GetMeshInstanceProxy(const Handle<MeshInstanceProxy>& meshInstanceHandle)
	{
		return s_RMData->MeshInstanceProxy.Get(meshInstanceHandle);
	}

	Handle<BindGroupLayout> ResourceManager::CreateBindGroupLayout(const BindGroupLayout::BindGroupLayoutInfo& info)
	{
		return s_RMData->BindGroupLayouts.Emplace(info);
	}

	BindGroupLayout* ResourceManager::GetBindGroupLayout(const Handle<BindGroupLayout>& handle)
	{
		return s_RMData->BindGroupLayouts.Get(handle);
	}

	void ResourceManager::DestroyBindGroupLayout(Handle<BindGroupLayout>& handle)
	{
		s_RMData->BindGroupLayouts.Remove(handle);
	}

	const RendererConfiguration& ResourceManager::GetRendererConfig()
	{
		static RendererConfiguration config;
		return config;
	}

	uint32_t ResourceManager::GetFramesInFlight()
	{
		return GetRendererConfig().FramesInFlight;
	}

	VkDescriptorSet ResourceManager::AllocateDescriptorSet(Handle<BindGroupLayout> handle)
	{
		BindGroupLayout* bindGroupLayout = ResourceManager::GetBindGroupLayout(handle);
		if (!bindGroupLayout)
			return nullptr;

		VkDescriptorSetLayout vkLayout = bindGroupLayout->GetVulkanDescriptorSetLayout();
		VkDescriptorSet descriptorSet = s_RMData->PersistentDescriptorAllocator->Allocate(vkLayout);

		return descriptorSet;
	}
}


