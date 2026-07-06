#pragma once
#include "Renderer/DescriptorAllocation.h"
#include "Core/Handle.h"
#include "Renderer/GPUBuffer.h"
#include "Renderer/AccelerationStructure.h"
#include "Renderer/Resources/MeshProxy.h"
#include "Renderer/Resources/BindGroup.h"
#include "Core/Window.h"
#include "Renderer/Texture.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Pipeline.h"
namespace cerberus
{
	struct RendererConfiguration
	{
		const uint32_t FramesInFlight = 3;
		const uint32_t BrdfLutSize = 256;
		const uint32_t EnvironmentMapResolution = 1024;
		const uint32_t IrradianceMapComputeSamples = 1024;
		const uint32_t IrradianceMapSize = 32;
		const uint32_t ShadowMapResolution = 4096;
		const uint32_t ShadowCascadesCount = 4;
	};

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

		static Handle<MeshSourceProxy> CreateMeshSourceProxy(const void* vertexData, uint32_t vertexSize, const void* indexData, uint32_t indexSize, const std::vector<Submesh>& submeshes);
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

		// TODO: new Textures and Images
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

		static Handle<RayTracingPipeline> CreateRayTracingPipeline(const PipelineSpecification& pipelineSpec);
		static void DestroyRayTracingPipeline(const Handle<RayTracingPipeline> handle);
		static RayTracingPipeline* GetRayTracingPipeline(const Handle<RayTracingPipeline> handle);
	
		static const RendererConfiguration& GetRendererConfig();

		static VkDescriptorSet AllocateDescriptorSet(Handle<BindGroupLayout> Layout, DescriptorAllocationLifetime lifetime);
		static VkDescriptorSet AllocateDescriptorSet(Handle<BindGroupLayout> Layout, bool persistent = true);
		static VkDescriptorSet AllocateTransientDescriptorSet(Handle<BindGroupLayout> Layout);
		static VkDescriptorSet AllocatePersistentDescriptorSet(Handle<BindGroupLayout> Layout);

		static uint32_t GetFramesInFlight();

		static void ResetDescriptorPool();
	};
}