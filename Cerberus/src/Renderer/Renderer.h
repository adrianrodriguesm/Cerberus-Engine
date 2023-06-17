#pragma once
#include "Renderer/RendererContext.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/VertexBuffer.h"
#include "Renderer/IndexBuffer.h"
#include "Renderer/CommandBuffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Mesh.h"
#include "Renderer/UniformBufferSet.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/SceneRenderer/SceneRenderer.h"
#include "Renderer/Backend/BindGroup.h"
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
	
	class Renderer
	{
	public:
		static void Init();
		static void Shutdown();

		// Submit Command to the RenderQueue
		template<typename FuncT>
		static void Submit(FuncT&& func)
		{
			auto renderCommand = [](void* ptr) 
			{
				auto pFunction = (FuncT*)ptr;
				// Execute Function
				(*pFunction)();

				// NOTE: Instead of destroying we could try and enforce all items to be trivally destructible
				// however some items like uniforms which contain std::strings still exist for now
				// static_assert(std::is_trivially_destructible_v<FuncT>, "FuncT must be trivially destructible");
				pFunction->~FuncT();
			};
			auto functionMemoryAllocation = GetRenderCommandQueue().Allocate(renderCommand, sizeof(func));
			// Construct function (emplace new)
			new (functionMemoryAllocation) FuncT(std::forward<FuncT>(func));
		}

		template<typename FuncT>
		static void SubmitResourceFree(FuncT&& func)
		{
			auto renderCommand = [](void* ptr) 
			{
				auto pFunction = (FuncT*)ptr;
				// Execute Function
				(*pFunction)();

				// NOTE: Instead of destroying we could try and enforce all items to be trivally destructible
				// however some items like uniforms which contain std::strings still exist for now
				// static_assert(std::is_trivially_destructible_v<FuncT>, "FuncT must be trivially destructible");
				pFunction->~FuncT();
			};

			Submit([renderCommand, func]() 
			{
				const uint32_t index = Renderer::GetCurrentFrameIndex();
				auto functionMemoryAllocation = GetRenderResourceReleaseQueue(index).Allocate(renderCommand, sizeof(func));
				// Construct function (emplace new)
				new (functionMemoryAllocation) FuncT(std::forward<FuncT>((FuncT&&)func));
			});
		}
		// Prepare and end rendering
		static void BeginFrame();
		static void EndFrame();
		// ImGui
		static void InitImGui();
		static void ShutdownImGui();
		static void RenderImGui();
		static void BeginImGui(CommandBuffer* commandBuffer);
		static void EndImGui(CommandBuffer* commandBuffer);
		static void SetImGuiColorTheme();
		// Renderer Queue
		static void ExecuteRenderQueue();
		static void ReleaseResourceFromQueue(uint32_t index);
		static void ReleaseAllResources();
		// Actual Rendering
		// -- Render Passes
		static void BeginRenderPass(CommandBuffer* CommandBuffer, Handle<RenderPass> renderPass, Handle<Framebuffer> framebuffer, bool explicitClear = false);
		static void EndRenderPass(CommandBuffer* CommandBuffer);
		// -- Descriptor's
		static void UpdateMaterialForRendering(Ref<Material> material, Ref<UniformBufferSet> uniformBufferSet);
		static VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout& layout, bool persistent = false);
		// -- Render Geometry
		static void RenderFullscreenQuad(CommandBuffer* commandBuffer, Handle<GraphicsPipeline> pipeline, Handle<BindGroup> material = {});
		/** /
		static void RenderMesh(Ref<CommandBuffer> CommandBuffer, Ref<UniformBufferSet> uniformBufferSet, Ref<Pipeline> pipeline, Ref<Mesh> mesh, Ref<MaterialTable> materialTable, glm::mat4& transform);
		static void RenderMeshWithMaterial(Ref<CommandBuffer> CommandBuffer, Ref<UniformBufferSet> uniformBufferSet, Ref<Pipeline> pipeline, Ref<Mesh> mesh, glm::mat4& transform, Ref<Material> material, const Buffer& aditionalUniform = {});
		/**/
		// -- Dispatch Compute Shader
		static void DispatchComputeShader(Ref<CommandBuffer> CommandBuffer, Ref<ComputePipeline> pipeline, Ref<UniformBufferSet> uniformBufferSet, Ref<Material> material, const glm::ivec3& workGroups, Buffer additionalUniforms = {});
		// Command Buffers
		static void FlushCommandBuffer(VkCommandBuffer commandBuffer, bool compute = false, bool waitToCompute = false, bool detroyCommandBuffer = true);
		static VkCommandBuffer GetCommandBuffer(bool beginCommand = false, bool compute = false);
		static VkCommandBuffer CreateSecondaryCommandBuffer();
		// TODO: sort this out
		static CommandBuffer* GetMainCommandBuffer(bool swapchain = false);
		// -- Environment Map
		// Return a pair with <FilteredMap, IrradianceMap>
		static Ref<EnvironmentMap> CreateEnvironmentMap(const std::string& filepath);
		static void SetSceneEnvironment(SceneRenderer* sceneRenderer, const Ref<EnvironmentMap>& environment, const Ref<Image>& shadowImage);
		// Getter's
		static uint32_t GetCurrentFrameIndex();
		static RendererConfig& GetRendererConfig();
		static ShaderLibrary& GetShaderLibrary();
		static Ref<RendererContext> GetRenderContext();
		static VkPhysicalDeviceLimits GetDeviceLimits();
		static Ref<Texture2D> GetWhiteTexture();
		static RenderQueue& GetRenderResourceReleaseQueue(uint32_t index);
		static Ref<MaterialAsset> GetDefaultPBRMaterial();
		static Ref<EnvironmentMap> GetEmptyEnvironmentMap();
		static VkDescriptorSetLayout CreateDescriptorSetLayout(VkDescriptorSetLayoutCreateInfo* info);
	private:
		static RenderQueue& GetRenderCommandQueue();
	
	};
}