#include "pch.h"

#include "Renderer.h"
#include "Renderer/Core/RendererContext.h"
#include "Renderer/Core/RendererResources.h"
#include "Renderer/Core/RendererRuntimeState.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Renderer/Environment/EnvironmentMapBaker.h"
#include "Renderer/Descriptor/DescriptorManager.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/Memory/DeferredResourceDeletionQueue.h"
#include "Renderer/Memory/ResourceAllocator.h"

#include "Core/Application.h"
#include "Core/Window.h"
#include "ImGui/ImGuiRenderer.h"

namespace cerberus
{
	struct RendererData
	{
		RendererCommandState Commands;
		RendererFrameState Frame;
		Scope<EnvironmentMapBaker> EnvironmentBaker;
	};

	static RendererData* s_Data;

	void Renderer::Init()
	{
		s_Data = new RendererData();

		auto vulkanDevice = RendererContext::GetCurrentDevice();
		s_Data->Frame.DeviceLimits = vulkanDevice->GetPhysicalDeviceLimits();

		RenderQueueManager::Init();
		DeferredResourceDeletionQueue::Init(Renderer::GetRendererConfig().FramesInFlight);
		DescriptorManager::Init();
		ResourceManager::Init();

		s_Data->Commands.GraphicsCommandPool = CreateScope<CommandPool>(
			CommandPoolQueueType::Graphics,
			VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			"Renderer Graphics Command Pool");
		s_Data->Commands.TransientExecutor = CreateScope<TransientCommandExecutor>(
			*s_Data->Commands.GraphicsCommandPool);

		RendererResources::Init();
		s_Data->EnvironmentBaker = CreateScope<EnvironmentMapBaker>(
			*s_Data->Commands.TransientExecutor);

		s_Data->Commands.MainCommandBuffer = CreateScope<CommandBuffer>(
			s_Data->Commands.GraphicsCommandPool.get(),
			ResourceManager::GetFramesInFlight(),
			"Main Command Buffer");
		s_Data->Commands.SwapchainCommandBuffer = CreateScope<CommandBuffer>(true, "Swapchain");

		// Compile shaders and upload/create default GPU resources queued by RendererResources.
		RenderQueueManager::ExecuteRenderQueue();
		ImGuiRenderer::Init(*s_Data->Commands.GraphicsCommandPool);
	}

	void Renderer::Shutdown()
	{
		auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		auto WaitAndDrain = [&]()
		{
			vkDeviceWaitIdle(vulkanDevice);
			RenderQueueManager::ExecuteRenderQueue();
			DeferredResourceDeletionQueue::FlushAll();
		};

		// Make sure no GPU/render-thread work is pending before teardown.
		WaitAndDrain();

		// The baker borrows the transient executor and owns no baked assets.
		s_Data->EnvironmentBaker.reset();

		// Release renderer-owned ResourceManager resources while all services are live.
		RendererResources::Shutdown();
		WaitAndDrain();

		ImGuiRenderer::Shutdown();

		s_Data->Commands.MainCommandBuffer.reset();
		s_Data->Commands.SwapchainCommandBuffer.reset();
		WaitAndDrain();

		// Resource wrappers may enqueue native Vulkan/VMA destruction.
		ResourceManager::Shutdown();
		WaitAndDrain();

		// Descriptor pools/layout cache outlive every bind group and pipeline wrapper.
		DescriptorManager::Shutdown();
		WaitAndDrain();

		// The executor borrows the command pool, so this order is intentional.
		s_Data->Commands.TransientExecutor.reset();
		s_Data->Commands.GraphicsCommandPool.reset();
		WaitAndDrain();

		const auto& stats = ResourceAllocator::GetStats();
		NV_CORE_WARN("Memory allocated: {0}", stats.Used);
		NV_CORE_WARN("Memory free: {0}", stats.Free);

		DeferredResourceDeletionQueue::Shutdown();
		RenderQueueManager::Shutdown();

		delete s_Data;
		s_Data = nullptr;
	}

	void Renderer::BeginFrame()
	{
		// Reset frame-local descriptor pools only after the current frame fence has
		// completed and before any transient descriptor set is allocated.
		DescriptorManager::ResetTransientFramePools();
		s_Data->Frame.Statistics = {};
	}

	void Renderer::EndFrame()
	{
	}

	void Renderer::WaitIdleAndDrain()
	{
		CORE_ASSERT(s_Data, "Renderer is not initialized");

		// Execute queued recording/submission first, then wait for that work before
		// releasing any native object referenced by the active renderer.
		RenderQueueManager::ExecuteRenderQueue();
		VK_CHECK_RESULT(vkDeviceWaitIdle(RendererContext::GetCurrentVulkanDevice()));
		DeferredResourceDeletionQueue::FlushAll();
	}

	Ref<RendererContext> Renderer::GetRenderContext()
	{
		return Application::Get().GetWindow()->GetRendererContext();
	}

	CommandBuffer* Renderer::GetMainCommandBuffer(bool swapchain)
	{
		return swapchain
			? s_Data->Commands.SwapchainCommandBuffer.get()
			: s_Data->Commands.MainCommandBuffer.get();
	}

	TransientCommandExecutor& Renderer::GetTransientCommandExecutor()
	{
		CORE_ASSERT(s_Data && s_Data->Commands.TransientExecutor,
			"Renderer transient command executor is not initialized");
		return *s_Data->Commands.TransientExecutor;
	}

	RendererFrameStatistics& Renderer::GetFrameStatistics()
	{
		CORE_ASSERT(s_Data, "Renderer is not initialized");
		return s_Data->Frame.Statistics;
	}

	CommandPool* Renderer::GetGraphicsCommandPool()
	{
		CORE_ASSERT(s_Data && s_Data->Commands.GraphicsCommandPool,
			"Renderer graphics CommandPool is not initialized");
		return s_Data->Commands.GraphicsCommandPool.get();
	}

	RendererConfig& Renderer::GetRendererConfig()
	{
		static RendererConfig config;
		return config;
	}

	Ref<EnvironmentMap> Renderer::CreateEnvironmentMap(const std::string& filepath)
	{
		CORE_ASSERT(s_Data && s_Data->EnvironmentBaker,
			"EnvironmentMapBaker is not initialized");

		const RendererConfig& config = GetRendererConfig();
		EnvironmentMapBakeSpecification specification;
		specification.EnvironmentResolution = config.EnvironmentMapResolution;
		specification.IrradianceResolution = config.IrradianceMapSize;
		specification.IrradianceSampleCount = config.IrradianceMapComputeSamples;
		return s_Data->EnvironmentBaker->Bake(filepath, specification);
	}

	uint32_t Renderer::GetCurrentFrameIndex()
	{
		return Window::GetCurrentSwapchain()->GetCurrentBufferIndex();
	}

	VkPhysicalDeviceLimits Renderer::GetDeviceLimits()
	{
		return s_Data->Frame.DeviceLimits;
	}
}
