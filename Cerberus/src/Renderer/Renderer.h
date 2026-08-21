#pragma once
#include "Renderer/RendererContext.h"
#include "Renderer/CommandBuffer.h"
#include "Renderer/CommandList.h"
#include "Renderer/TransientCommandExecutor.h"
namespace cerberus
{
	struct EnvironmentMap;

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
		// Rare lifecycle boundary used when replacing a complete SceneRenderer.
		// This is intentionally blocking and must never be called once per frame.
		static void WaitIdleAndDrain();

		// Command infrastructure. Renderer owns the services; command lists record
		// through borrowed native command buffers and never submit themselves.
		static CommandBuffer* GetMainCommandBuffer(bool swapchain = false);
		static TransientCommandExecutor& GetTransientCommandExecutor();
		static RendererFrameStatistics& GetFrameStatistics();

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
	};
}
