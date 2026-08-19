#pragma once
#include "Renderer/RendererContext.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/VertexBuffer.h"
#include "Renderer/IndexBuffer.h"
#include "Renderer/CommandBuffer.h"
#include "Renderer/CommandPool.h"
#include "Renderer/TransientCommandBuffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Mesh.h"
#include "Renderer/UniformBufferSet.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/SceneRenderer/SceneRenderer.h"
#include "Renderer/Resources/BindGroup.h"
#include "Renderer/DrawStreamBuilder.h"
namespace cerberus
{
	struct RendererConfig
	{
		const uint32_t FramesInFlight = 3;
		const uint32_t BrdfLutSize = 256;
		const uint32_t EnvironmentMapResolution = 1024;
		const uint32_t IrradianceMapComputeSamples = 1024;
		const uint32_t IrradianceMapSize = 32;
		const uint32_t ShadowMapResolution = 4096;
		const uint32_t ShadowCascadesCount = 4;
	};

	typedef void(*RenderCommandFn)(void*);
	
	// Public facade for global renderer services.
	//
	// Renderer owns process-wide rendering state and frame lifecycle helpers.
	// Per-scene renderable data belongs in RenderScene / SceneRenderer; native
	// Vulkan/VMA delayed destruction belongs in DeferredResourceDeletionQueue; engine
	// resource wrappers belong in ResourceManager.
	class Renderer
	{
	public:
		static void Init();
		static void Shutdown();

		// Prepare and end rendering
		static void BeginFrame();
		static void EndFrame();
		// Actual Rendering
		// -- Render Passes
		static void BeginRenderPass(CommandBuffer* CommandBuffer, Handle<RenderPass> renderPass, const DrawArea& drawArea);
		static void EndRenderPass(CommandBuffer* CommandBuffer);
		// -- Actives buffers to be used for render
		static void SetActiveVertexAndIndexBuffer(CommandBuffer* commandBuffer, const Handle<GPUBuffer> hVertexBuffer, const Handle<GPUBuffer> hInderBuffer);
		// -- Descriptor's
		static void UpdateMaterialForRendering(Ref<Material> material, Ref<UniformBufferSet> uniformBufferSet);
		// -- Render Geometry
		static void RenderFullscreenQuad(CommandBuffer* commandBuffer, Handle<GraphicsPipeline> pipeline, Handle<BindGroup> material = {});
		static void RenderRenderPass(CommandBuffer* commandBuffer, const DrawArea& drawArea, std::vector<uint32_t> drawStream);
		static void RenderMeshInstances(CommandBuffer* commandBuffer, const DrawArea& drawArea, std::vector<Handle<MeshInstanceProxy>> drawStream);
		/** /
		static void RenderMesh(Ref<CommandBuffer> CommandBuffer, Ref<UniformBufferSet> uniformBufferSet, Ref<Pipeline> pipeline, Ref<Mesh> mesh, Ref<MaterialTable> materialTable, glm::mat4& transform);
		static void RenderMeshWithMaterial(Ref<CommandBuffer> CommandBuffer, Ref<UniformBufferSet> uniformBufferSet, Ref<Pipeline> pipeline, Ref<Mesh> mesh, glm::mat4& transform, Ref<Material> material, const Buffer& aditionalUniform = {});
		/**/
		// -- Dispatch Compute Shader
		static void DispatchComputeShader(Ref<CommandBuffer> CommandBuffer, Ref<ComputePipeline> pipeline, Ref<UniformBufferSet> uniformBufferSet, Ref<Material> material, const glm::ivec3& workGroups, Buffer additionalUniforms = {});
		// Command Buffers
		static TransientCommandBuffer BeginTransientCommandBuffer(bool compute = false);
		static void SubmitTransientCommandBuffer(TransientCommandBuffer& commandBuffer, bool waitForQueueIdle = false);
		static CommandBuffer* GetMainCommandBuffer(bool swapchain = false);

		// -- Environment Map
		// Builds the source equirectangular texture plus filtered radiance and
		// irradiance cubemaps owned by the returned EnvironmentMap asset.
		static Ref<EnvironmentMap> CreateEnvironmentMap(const std::string& filepath);
		// Getter's
		static uint32_t GetCurrentFrameIndex();
		static RendererConfig& GetRendererConfig();
		static Ref<RendererContext> GetRenderContext();
		static VkPhysicalDeviceLimits GetDeviceLimits();
	private:
		friend class CommandBuffer;
	private:
		static CommandPool* GetGraphicsCommandPool();
		static CommandPool* GetComputeCommandPool();
	};
}
