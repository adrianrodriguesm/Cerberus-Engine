#include "pch.h"
#include "ResourceManager.h"
#include "ResourceAllocator.h"
#include "Core/Pool.h"
#include "Renderer/Descriptor.h"
namespace cerberus
{
	struct ResourceManagerData
	{
		Pool<GPUBuffer> GPUBuffers;
		Pool<AccelerationStructure> AccelerationStructures;
		Pool<MeshSourceProxy> MeshSourceProxy;
		Pool<MeshInstanceProxy> MeshInstanceProxy;
		Pool<BindGroupLayout> BindGroupLayouts;
		Pool<BindGroup> BindGroups;
		Pool<Image> Images;
		Pool<ImageView> ImageViews;
		Pool<Texture2D> Textures2D;
		Pool<TextureCube> TexturesCube;
		Pool<RenderTargetTexture> RenderTargets;
		Pool<RenderPass> RenderPasses;
		Pool<Framebuffer> Framebuffers;
		Pool<GraphicsPipeline> GraphicsPipelines;
		Pool<RayTracingPipeline> RayTracingPipelines;

		DescriptorAllocator* PersistentDescriptorAllocator;
		DescriptorAllocator* DescriptorAllocator;
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
		descriptorAllocatorSpec.Flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		s_RMData->PersistentDescriptorAllocator = new DescriptorAllocator(descriptorAllocatorSpec);
		descriptorAllocatorSpec.IsPersistent = false;
		s_RMData->DescriptorAllocator = new DescriptorAllocator(descriptorAllocatorSpec);
	}
	void ResourceManager::Shutdown()
	{
		s_RMData->Textures2D.Clear();
		s_RMData->TexturesCube.Clear();
		s_RMData->RenderTargets.Clear();
		s_RMData->GPUBuffers.Clear();
		s_RMData->Images.Clear();
		s_RMData->ImageViews.Clear();
		s_RMData->BindGroupLayouts.Clear();
		s_RMData->BindGroups.Clear();
		s_RMData->RenderPasses.Clear();
		s_RMData->Framebuffers.Clear();
		s_RMData->GraphicsPipelines.Clear();
		s_RMData->MeshInstanceProxy.Clear();
		s_RMData->MeshSourceProxy.Clear();
		s_RMData->AccelerationStructures.Clear();
		s_RMData->RayTracingPipelines.Clear();

		delete s_RMData->PersistentDescriptorAllocator;
		delete s_RMData->DescriptorAllocator;

		delete s_RMData;
	}

	Handle<AccelerationStructure> ResourceManager::CreateAccelerationStructure(const AccelerationStructureSpecification& asSpecification)
	{
		return s_RMData->AccelerationStructures.Emplace(asSpecification);
	}

	AccelerationStructure* ResourceManager::GetAccelerationStructure(const Handle<AccelerationStructure> asHandle)
	{
		return s_RMData->AccelerationStructures.Get(asHandle);
	}
	void ResourceManager::DestroyAccelerationStructure(const Handle<AccelerationStructure> handle)
	{

		s_RMData->AccelerationStructures.Remove(handle);

	}

	Handle<GPUBuffer> ResourceManager::CreateBuffer(const GPUBufferSpecification& gpuBufferSpec)
	{
		return s_RMData->GPUBuffers.Emplace(gpuBufferSpec);
	}

	void ResourceManager::DestroyBuffer(const Handle<GPUBuffer> handle)
	{
		s_RMData->GPUBuffers.Remove(handle);
	}

	GPUBuffer* ResourceManager::GetBuffer(const Handle<GPUBuffer> buffer)
	{
		return s_RMData->GPUBuffers.Get(buffer);
	}

	Handle<MeshSourceProxy> ResourceManager::CreateMeshSourceProxy(const void* vertexData, uint32_t vertexSize, const void* indexData, uint32_t indexSize, const std::vector<Submesh>& submeshes)
	{
		return s_RMData->MeshSourceProxy.Emplace(vertexData, vertexSize, indexData, indexSize, submeshes);
	}

	void ResourceManager::DestroyMeshSourceProxy(const Handle<MeshSourceProxy> handle)
	{
		s_RMData->MeshSourceProxy.Remove(handle);
	}
	MeshSourceProxy* ResourceManager::GetMeshSourceProxy(const Handle<MeshSourceProxy> meshSourceProxyHandle)
	{
		return s_RMData->MeshSourceProxy.Get(meshSourceProxyHandle);
	}

	Handle<MeshInstanceProxy> ResourceManager::CreateMeshInstanceProxy(const Handle<MeshSourceProxy> meshSource, uint32_t submeshIndex)
	{
		return s_RMData->MeshInstanceProxy.Emplace(meshSource, submeshIndex, math::MatFactory::CreateIdentityMat4());
	}
	void ResourceManager::DestroyMeshInstanceProxy(const Handle<MeshInstanceProxy> handle)
	{
		s_RMData->MeshInstanceProxy.Remove(handle);
	}
	MeshInstanceProxy* ResourceManager::GetMeshInstanceProxy(const Handle<MeshInstanceProxy> meshInstanceHandle)
	{
		return s_RMData->MeshInstanceProxy.Get(meshInstanceHandle);
	}

	Handle<BindGroupLayout> ResourceManager::CreateBindGroupLayout(const BindGroupLayout::BindGroupLayoutInfo& info)
	{
		return s_RMData->BindGroupLayouts.Emplace(info);
	}

	BindGroupLayout* ResourceManager::GetBindGroupLayout(const Handle<BindGroupLayout> handle)
	{
		return s_RMData->BindGroupLayouts.Get(handle);
	}

	void ResourceManager::DestroyBindGroupLayout(const Handle<BindGroupLayout> handle)
	{
		s_RMData->BindGroupLayouts.Remove(handle);
	}

	Handle<BindGroup> ResourceManager::CreateBindGroup(const BindGroupSpecification& spec)
	{
		return s_RMData->BindGroups.Emplace(spec);
	}

	void ResourceManager::DestroyBindGroup(const Handle<BindGroup> handle)
	{
		s_RMData->BindGroups.Remove(handle);
	}

	BindGroup* ResourceManager::GetBindGroup(const Handle<BindGroup> handle)
	{
		return s_RMData->BindGroups.Get(handle);
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

	VkDescriptorSet ResourceManager::AllocateDescriptorSet(Handle<BindGroupLayout> handle, bool persistent /* = false */)
	{
		BindGroupLayout* bindGroupLayout = ResourceManager::GetBindGroupLayout(handle);
		if (!bindGroupLayout)
			return nullptr;

		VkDescriptorSetLayout vkLayout = bindGroupLayout->GetVulkanDescriptorSetLayout();

		VkDescriptorSet descriptorSet = persistent ? s_RMData->PersistentDescriptorAllocator->Allocate(vkLayout) : 
													 s_RMData->DescriptorAllocator->Allocate(vkLayout);

		return descriptorSet;
	}

	Handle<Texture2D> ResourceManager::CreateTexture2D(ImageFormat format, const uint32_t width, const uint32_t height, const void* data, TextureProperties properties)
	{
		return s_RMData->Textures2D.Emplace(format, width, height, data, properties);
	}
	void ResourceManager::DestroyTexture2D(Handle<Texture2D> handle)
	{
		s_RMData->Textures2D.Remove(handle);
	}
	Texture2D* ResourceManager::GetTexture2D(const Handle<Texture2D> handle)
	{
		return s_RMData->Textures2D.Get(handle);
	}

	Handle<TextureCube> ResourceManager::CreateTextureCube(ImageFormat format, uint32_t width, uint32_t height, TextureProperties properties, const void* data)
	{
		return s_RMData->TexturesCube.Emplace(format, width, height, properties, data);
	}
	
	void ResourceManager::DestroyTextureCube(const Handle<TextureCube> handle)
	{
		s_RMData->TexturesCube.Remove(handle);
	}
	
	TextureCube* ResourceManager::GetTextureCube(const Handle<TextureCube> handle)
	{
		return s_RMData->TexturesCube.Get(handle);
	}

	Handle<RenderTargetTexture> ResourceManager::CreateRenderTarget(const RenderTargetProperties& prop)
	{
		return s_RMData->RenderTargets.Emplace(prop);
	}
	void ResourceManager::DestroyRenderTarget(const Handle<RenderTargetTexture> handle)
	{
		s_RMData->RenderTargets.Remove(handle);
	}
	RenderTargetTexture* ResourceManager::GetRenderTarget(const Handle<RenderTargetTexture> handle) 
	{
		return s_RMData->RenderTargets.Get(handle);
	}

	Handle<Image> ResourceManager::CreateImage(const ImageSpecification& specification)
	{
		return s_RMData->Images.Emplace(specification);
	}
	void ResourceManager::DestroyImage(const Handle<Image> handle)
	{
		s_RMData->Images.Remove(handle);
	}
	Image* ResourceManager::GetImage(const Handle<Image> handle)
	{
		return s_RMData->Images.Get(handle);
	}

	Handle<ImageView> ResourceManager::CreateImageView(const ImageViewProperties& imgViewProp)
	{
		return s_RMData->ImageViews.Emplace(imgViewProp);
	}
	void ResourceManager::DestroyImageView(const Handle<ImageView> handle)
	{
		s_RMData->ImageViews.Remove(handle);
	}
	ImageView* ResourceManager::GetImageView(const Handle<ImageView> handle)
	{
		return s_RMData->ImageViews.Get(handle);
	}

	Handle<RenderPass> ResourceManager::CreateRenderPass(const RenderPassSpecification& renderSpec)
	{
		return s_RMData->RenderPasses.Emplace(renderSpec);
	}
	
	void ResourceManager::DestroyRenderPass(const Handle<RenderPass> handle)
	{
		s_RMData->RenderPasses.Remove(handle);
	}
	
	RenderPass* ResourceManager::GetRenderPass(const Handle<RenderPass> handle)
	{
		return s_RMData->RenderPasses.Get(handle);
	}


	Handle<Framebuffer> ResourceManager::CreateFramebuffer(const FramebufferSpecification& framebufferSpec)
	{
		return s_RMData->Framebuffers.Emplace(framebufferSpec);
	}

	void ResourceManager::DestroyFramebuffer(const Handle<Framebuffer> handle)
	{
		s_RMData->Framebuffers.Remove(handle);
	}

	Framebuffer* ResourceManager::GetFramebuffer(const Handle<Framebuffer> handle)
	{
		return s_RMData->Framebuffers.Get(handle);
	}

	Handle<GraphicsPipeline> ResourceManager::CreateGraphicsPipeline(const PipelineSpecification& pipelineSpec)
	{
		return s_RMData->GraphicsPipelines.Emplace(pipelineSpec);
	}

	void ResourceManager::DestroyGraphicsPipeline(const Handle<GraphicsPipeline> handle)
	{
		s_RMData->GraphicsPipelines.Remove(handle);
	}

	GraphicsPipeline* ResourceManager::GetGraphicsPipeline(const Handle<GraphicsPipeline> handle)
	{
		return s_RMData->GraphicsPipelines.Get(handle);
	}

	Handle<RayTracingPipeline> ResourceManager::CreateRayTracingPipeline(const PipelineSpecification& pipelineSpec)
	{
		return s_RMData->RayTracingPipelines.Emplace(pipelineSpec);
	}

	void ResourceManager::DestroyRayTracingPipeline(const Handle<RayTracingPipeline> handle)
	{
		s_RMData->RayTracingPipelines.Remove(handle);
	}

	RayTracingPipeline* ResourceManager::GetRayTracingPipeline(const Handle<RayTracingPipeline> handle)
	{
		return s_RMData->RayTracingPipelines.Get(handle);
	}

	void ResourceManager::ResetDescriptorPool()
	{
		s_RMData->DescriptorAllocator->ResetPool();
	}
}


