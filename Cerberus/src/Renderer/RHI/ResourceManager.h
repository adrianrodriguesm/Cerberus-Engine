#pragma once
#include "Core/Base.h"
#include "Renderer/Descriptor/DescriptorAllocation.h"
#include "Core/Handle.h"
#include "Renderer/RHI/Buffers/GPUBuffer.h"
#include "Renderer/RayTracing/AccelerationStructure.h"
#include "Renderer/Scene/Proxies/MeshProxy.h"
#include "Renderer/Descriptor/BindGroup.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/RHI/Image.h"
#include "Renderer/RHI/RenderPass.h"
#include "Renderer/RHI/Framebuffer.h"
#include "Renderer/RHI/Pipeline/Pipeline.h"

#include <span>
namespace cerberus
{
	class ComputePipeline;
	class Shader;

	class ResourceManager
	{
	public:
		static void Init();
		static void Shutdown();

		static Handle<AccelerationStructure> CreateAccelerationStructure(const AccelerationStructureSpecification& asSpecification);
		static void DestroyAccelerationStructure(const Handle<AccelerationStructure> as);
		static AccelerationStructure* GetAccelerationStructure(const Handle<AccelerationStructure> as);
		
		static Handle<GPUBuffer> CreateBuffer(const GPUBufferSpecification& gpuBufferSpec);
		static void DestroyBuffer(const Handle<GPUBuffer> buffer);
		static GPUBuffer* GetBuffer(const Handle<GPUBuffer> buffe);

		static Handle<MeshSourceProxy> CreateMeshSourceProxy(const void* vertexData, uint32_t vertexSize, const void* indexData, uint32_t indexSize, std::span<const Submesh> submeshes);
		static void DestroyMeshSourceProxy(const Handle<MeshSourceProxy> meshSourceProxyHandle);
		static MeshSourceProxy* GetMeshSourceProxy(const Handle<MeshSourceProxy> meshSourceProxyHandle);

		static Handle<MeshInstanceProxy> CreateMeshInstanceProxy(
			Handle<MeshSourceProxy> meshSource,
			uint32_t submeshIndex,
			uint32_t materialProxyIndex = InvalidMaterialProxyIndex);
		static void DestroyMeshInstanceProxy(const Handle<MeshInstanceProxy> meshInstance);
		static MeshInstanceProxy* GetMeshInstanceProxy(const Handle<MeshInstanceProxy> meshInstance);

		static Handle<BindGroupLayout> CreateBindGroupLayout(const BindGroupLayout::BindGroupLayoutInfo& info);
		static void DestroyBindGroupLayout(const Handle<BindGroupLayout> handle);
		static BindGroupLayout* GetBindGroupLayout(const Handle<BindGroupLayout> handle);


		static Handle<BindGroup> CreateBindGroup(const BindGroupSpecification& spec);
		static void DestroyBindGroup(const Handle<BindGroup> handle);
		static BindGroup* GetBindGroup(const Handle<BindGroup> handle);

		// TextureAsset owns source pixels; Texture2D owns the corresponding GPU resource.
		static Handle<Texture2D> CreateTexture2D(
			const Ref<TextureAsset>& textureAsset,
			TextureColorSpace colorSpace = TextureColorSpace::AssetDefault);
		static Handle<Texture2D> CreateTexture2D(ImageFormat format, const uint32_t width, const uint32_t height, const void* data, TextureProperties properties = {});
		static void DestroyTexture2D(const Handle<Texture2D> handle);
		static Texture2D* GetTexture2D(const Handle<Texture2D> handle);

		static Handle<TextureCube> CreateTextureCube(ImageFormat format, uint32_t width, uint32_t height, TextureProperties properties = TextureProperties(), const void* data = nullptr);
		static void DestroyTextureCube(const Handle<TextureCube> handle);
		static TextureCube* GetTextureCube(const Handle<TextureCube> handle);

		static Handle<RenderTargetTexture> CreateRenderTarget(const RenderTargetProperties& prop);
		static void DestroyRenderTarget(const Handle<RenderTargetTexture> handle);
		static RenderTargetTexture* GetRenderTarget(const Handle<RenderTargetTexture> handle);

		static Handle<Image> CreateImage(const ImageSpecification& specification);
		static void DestroyImage(const Handle<Image> handle);
		static Image* GetImage(const Handle<Image> handle);


		static Handle<ImageView> CreateImageView(const ImageViewProperties& imgViewProp);
		static void DestroyImageView(const Handle<ImageView> handle);
		static ImageView* GetImageView(const Handle<ImageView> handle);

		static Handle<RenderPass> CreateRenderPass(const RenderPassSpecification& renderSpec);
		static void DestroyRenderPass(const Handle<RenderPass> handle);
		static RenderPass* GetRenderPass(const Handle<RenderPass> handle);


		static Handle<Framebuffer> CreateFramebuffer(const FramebufferSpecification& framebufferSpec);
		static void DestroyFramebuffer(const Handle<Framebuffer> handle);
		static Framebuffer* GetFramebuffer(const Handle<Framebuffer> handle);

		static Handle<GraphicsPipeline> CreateGraphicsPipeline(const PipelineSpecification& pipelineSpec);
		static void DestroyGraphicsPipeline(const Handle<GraphicsPipeline> handle);
		static GraphicsPipeline* GetGraphicsPipeline(const Handle<GraphicsPipeline> handle);

		static Handle<ComputePipeline> CreateComputePipeline(const Ref<Shader>& computeShader);
		static void DestroyComputePipeline(const Handle<ComputePipeline> handle);
		static ComputePipeline* GetComputePipeline(const Handle<ComputePipeline> handle);

		static Handle<RayTracingPipeline> CreateRayTracingPipeline(const PipelineSpecification& pipelineSpec);
		static void DestroyRayTracingPipeline(const Handle<RayTracingPipeline> handle);
		static RayTracingPipeline* GetRayTracingPipeline(const Handle<RayTracingPipeline> handle);
	
		static VkDescriptorSet AllocateDescriptorSet(Handle<BindGroupLayout> Layout, DescriptorAllocationLifetime lifetime);

		static uint32_t GetFramesInFlight();
	};
}
