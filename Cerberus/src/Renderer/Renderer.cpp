#include "pch.h"
#include "Renderer.h"
#include "Core/Application.h"
#include "Renderer/ResourceAllocator.h"
#include "Renderer/UniformBufferSet.h"
#include "Renderer/ComputePipeline.h"
#include "Renderer/Descriptor.h"
#include "Utils/StringUtils.h"
#include "ImGui/ImGuizmo.h"
#include "ImGui/ImGuiColors.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/DeferredDeletionQueue.h"

#include "imgui.h"
#ifndef IMGUI_IMPL_API
#define IMGUI_IMPL_API
#endif
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan_textures.h"

namespace cerberus
{
	struct RendererData
	{
		// Render Command Pool (This pool is mostly is by the transfer queue but in Vulkan 
		// by default the graphics queue already includes the transfer queue)
		VkCommandPool CommandPool;
		VkCommandPool ComputeCommandPool;
		// Renderer Textures
		Handle<TextureCube> BlackCubeTexture;
		Handle<Texture2D> BlackTexture;
		Handle<Texture2D> WhiteTexture;
		Handle<Texture2D> BrdfLutTexture;
		// TODO: Switch to Bind group
		Ref<MaterialAsset> DefaultMaterial;
		// TODO: Move to Scene renderer class
		Ref<EnvironmentMap> EmptyEnvironment;

		std::unordered_map<SceneRenderer*, std::vector<std::vector<VkDescriptorSet>>> RendererDescriptorSetCache;
		VkDescriptorSet ActiveRendererDescriptorSet = nullptr;

		VkDescriptorPool ImGuiDescriptorPool;
		DescriptorAllocator* PersistentDescriptorAllocator;
		DescriptorAllocator* DescriptorAllocator;
		DescriptorLayoutCache* DescriptorLayoutCache;

		CommandBuffer* MainCommandBuffer;
		CommandBuffer* SwapchainCommandBuffer;

		struct WriteDescriptorData
		{
			uint32_t Set;
			std::vector<VkWriteDescriptorSet> WriteDescriptors;
		};

		// UniformBufferSet -> Shader Hash -> Frame -> Set -> WriteDescriptor
		std::unordered_map<UniformBufferSet*, std::unordered_map<uint64_t, std::vector<std::vector<std::vector<VkWriteDescriptorSet>>>>> UniformBufferWriteDescriptorCache;
		uint32_t DrawCallCount;

		Ref<ShaderLibrary> ShaderLibrary;

		VkPhysicalDeviceLimits DeviceLimits;
	};

	static RendererData* s_Data;

	static std::vector<VkCommandBuffer> s_ImGuiCommandBuffers;
	void Renderer::Init()
	{
		s_Data = new RendererData();

		auto vulkanDevice = RendererContext::GetCurrentDevice();
		

		s_Data->DeviceLimits = vulkanDevice->GetPhysicalDeviceLimits();

		ResourceManager::Init();
		RenderQueueManager::Init();
		DeferredDeletionQueue::Init(Renderer::GetRendererConfig().FramesInFlight);

		//////////////////////////////////
		// Create Compute Pool
		//////////////////////////////////
		auto& queueFamilyIndices = vulkanDevice->GetPhysicalDevice()->GetQueueFamilyIndices();
		VkCommandPoolCreateInfo poolComputeInfo = {};
		poolComputeInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolComputeInfo.queueFamilyIndex = queueFamilyIndices.Compute;	// Queue Family type that buffers from this command pool will use
		// VK_COMMAND_POOL_CREATE_TRANSIENT_BIT the command buffers allocated from the pool will be short-lived, meaning that they will be reset or freed in a relatively short timeframe
		// VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT allows any command buffer allocated from a pool to be individually reset to the initial state when calling vkBeginCommandBuffer
		poolComputeInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		// Create a Graphics Queue Family Command Pool
		VK_CHECK_RESULT(vkCreateCommandPool(vulkanDevice->GetVulkanDevice(), &poolComputeInfo, nullptr, &s_Data->ComputeCommandPool));
		//////////////////////////////////
		// Create Graphic Pool
		//////////////////////////////////
		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndices.Graphics;	// Queue Family type that buffers from this command pool will use
		// VK_COMMAND_POOL_CREATE_TRANSIENT_BIT the command buffers allocated from the pool will be short-lived, meaning that they will be reset or freed in a relatively short timeframe
		// VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT allows any command buffer allocated from a pool to be individually reset to the initial state when calling vkBeginCommandBuffer
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		// Create a Graphics Queue Family Command Pool
		VK_CHECK_RESULT(vkCreateCommandPool(vulkanDevice->GetVulkanDevice(), &poolInfo, nullptr, &s_Data->CommandPool));
		
		
		//////////////////////////////////
		// Create Descriptor Pool
		//////////////////////////////////
		DescriptorAllocatorSpecification descriptorAllocatorSpec;
		descriptorAllocatorSpec.NumberOfFrames = Renderer::GetRendererConfig().FramesInFlight;
		descriptorAllocatorSpec.MaxSets = 1000;
		descriptorAllocatorSpec.Flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		descriptorAllocatorSpec.IsPersistent = false;
		s_Data->DescriptorAllocator = new DescriptorAllocator(descriptorAllocatorSpec);
		descriptorAllocatorSpec.IsPersistent = true;
		s_Data->PersistentDescriptorAllocator = new DescriptorAllocator(descriptorAllocatorSpec);
		s_Data->DescriptorLayoutCache = new DescriptorLayoutCache();

		// SHADERS
		s_Data->ShaderLibrary = CreateRef<ShaderLibrary>();
		s_Data->ShaderLibrary->Load("Resources/Shaders/PBR.glsl", false);
		//s_Data->ShaderLibrary->Load("Resources/Shaders/SceneComposite.glsl", false);
		//s_Data->ShaderLibrary->Load("Resources/Shaders/EquirectangularToCubeMap.glsl", false);
		//s_Data->ShaderLibrary->Load("Resources/Shaders/EnvironmentMipFilter.glsl", false);
		//s_Data->ShaderLibrary->Load("Resources/Shaders/EnvironmentIrradiance.glsl", false);
		//s_Data->ShaderLibrary->Load("Resources/Shaders/BRDF.glsl", false);
		//s_Data->ShaderLibrary->Load("Resources/Shaders/Skybox.glsl", false);
		//s_Data->ShaderLibrary->Load("Resources/Shaders/PreDepth.glsl", false);
		//s_Data->ShaderLibrary->Load("Resources/Shaders/ShadowMap.glsl", false);


		// Textures
		s_Data->BlackCubeTexture = TextureFactory::CreateTextureCubeWithColor({ 0, 0, 0, 0 });
		s_Data->BlackTexture = TextureFactory::CreateTexture2DWithColor({ 0, 0, 0, 0 });
		s_Data->WhiteTexture = TextureFactory::CreateTexture2DWithColor({ 255, 255, 255, 255 });
		s_Data->BrdfLutTexture = TextureFactory::CreateBRDF();
		// EmptyEnvironment
#if OLD_RENDERER
		s_Data->EmptyEnvironment = CreateRef<EnvironmentMap>();
		s_Data->EmptyEnvironment->EquirectangularTex = s_Data->BlackTexture;
		s_Data->EmptyEnvironment->IrradianceMap = s_Data->BlackCubeTexture;
		s_Data->EmptyEnvironment->RadianceMap = s_Data->BlackCubeTexture;
		s_Data->EmptyEnvironment->DebugName = "Empty Environment";
#endif
		s_Data->MainCommandBuffer = new CommandBuffer(ResourceManager::GetFramesInFlight(), "Main Command Buffer");
		s_Data->SwapchainCommandBuffer = new CommandBuffer(true, "Swapchain");
	
		// Compile Shader and Create default textures
		RenderQueueManager::ExecuteRenderQueue();
		Renderer::InitImGui();

		s_Data->DefaultMaterial = CreateRef<MaterialAsset>();


		s_Data->DefaultMaterial->SetAlbedoColor({ 1.f, 0, 1.f });
	}

	void Renderer::Shutdown()
	{
		auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		// Make sure GPU work is finished before destroying GPU resources.
		vkDeviceWaitIdle(vulkanDevice);

		// ---------------------------------------------------------------------
		// 1. Drain any pending render commands / deferred frees while all systems
		//    are still alive.
		// ---------------------------------------------------------------------
		RenderQueueManager::ExecuteRenderQueue();
		DeferredDeletionQueue::FlushAll();

		// ---------------------------------------------------------------------
		// 2. Destroy renderer-owned ResourceManager resources.
		//    These calls may enqueue resource-free work internally.
		// ---------------------------------------------------------------------
		ResourceManager::DestroyTextureCube(s_Data->BlackCubeTexture);
		ResourceManager::DestroyTexture2D(s_Data->BlackTexture);
		ResourceManager::DestroyTexture2D(s_Data->WhiteTexture);
		ResourceManager::DestroyTexture2D(s_Data->BrdfLutTexture);

		s_Data->ShaderLibrary->ReleaseAllShaders();

		// Wait again because the Destroy* calls above may submit Vulkan work or
		// enqueue deferred frees that assume the GPU is idle.
		vkDeviceWaitIdle(vulkanDevice);

		// ---------------------------------------------------------------------
		// 3. Drain the frees generated by the Destroy* calls above.
		// ---------------------------------------------------------------------
		RenderQueueManager::ExecuteRenderQueue();
		DeferredDeletionQueue::FlushAll();

		// ---------------------------------------------------------------------
		// 4. Destroy non-ResourceManager renderer systems.
		// ---------------------------------------------------------------------
		ComputePipeline::ReleaseComputeFence();

		Renderer::ShutdownImGui();

		const auto& stats = ResourceAllocator::GetStats();
		NV_CORE_WARN("Memory allocated: {0}", stats.Used);
		NV_CORE_WARN("Memory free: {0}", stats.Free);

		vkDestroyCommandPool(vulkanDevice, s_Data->CommandPool, nullptr);
		vkDestroyCommandPool(vulkanDevice, s_Data->ComputeCommandPool, nullptr);

		delete s_Data->DescriptorAllocator;
		s_Data->DescriptorAllocator = nullptr;

		delete s_Data->PersistentDescriptorAllocator;
		s_Data->PersistentDescriptorAllocator = nullptr;

		delete s_Data->DescriptorLayoutCache;
		s_Data->DescriptorLayoutCache = nullptr;

		delete s_Data->MainCommandBuffer;
		s_Data->MainCommandBuffer = nullptr;

		delete s_Data->SwapchainCommandBuffer;
		s_Data->SwapchainCommandBuffer = nullptr;

		s_Data->RendererDescriptorSetCache.clear();

		// Destructors above can enqueue deferred Vulkan destruction work. Drain it
		// before ResourceManager and RendererContext/allocator start shutting down.
		vkDeviceWaitIdle(vulkanDevice);
		RenderQueueManager::ExecuteRenderQueue();
		DeferredDeletionQueue::FlushAll();

		// ---------------------------------------------------------------------
		// 5. ResourceManager shutdown must happen AFTER all queued ResourceManager
		//    destroy calls have been executed.
		// ---------------------------------------------------------------------
		ResourceManager::Shutdown();

		// If ResourceManager::Shutdown() generated deferred deletes, flush them
		// before DeferredDeletionQueue and VMA/RendererContext are destroyed.
		DeferredDeletionQueue::FlushAll();

		// DeferredDeletionQueue owns only queue storage at this point. It must not
		// execute queued deletion commands during shutdown.
		DeferredDeletionQueue::Shutdown();

		// RenderQueueManager::Shutdown() should ONLY delete the command queue.
		// It must NOT Execute() anything here anymore.
		RenderQueueManager::Shutdown();

		delete s_Data;
		s_Data = nullptr;
	}

	void Renderer::BeginFrame()
	{
		RenderQueueManager::Submit([]()
		{
			s_Data->DescriptorAllocator->ResetPool();
			s_Data->DrawCallCount = 0;
		});
	}

	void Renderer::EndFrame()
	{
	}

	void Renderer::InitImGui()
	{
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows


		io.Fonts->AddFontFromFileTTF("Resources/Fonts/opensans/OpenSans-Bold.ttf", 20.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
		io.FontDefault = io.Fonts->AddFontFromFileTTF("Resources/Fonts/opensans/OpenSans-Regular.ttf", 20.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();

		SetImGuiColorTheme();

		Application& app = Application::Get();
		GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow()->GetNativeWindow());

		Ref<Device> device = RendererContext::GetCurrentDevice();
		Ref<PhysicalDevice> physicalDevice = RendererContext::GetCurrentDevice()->GetPhysicalDevice();


		// Create Descriptor Pool
		VkDescriptorPoolSize pool_sizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 }
		};
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 100 * IM_ARRAYSIZE(pool_sizes);
		pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
		pool_info.pPoolSizes = pool_sizes;
		VK_CHECK_RESULT(vkCreateDescriptorPool(device->GetVulkanDevice(), &pool_info, nullptr, &s_Data->ImGuiDescriptorPool));

		// Setup Platform/Renderer bindings
		ImGui_ImplGlfw_InitForVulkan(window, true);
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = RendererContext::GetInstance();
		init_info.PhysicalDevice = physicalDevice->GetVulkanPhysicalDevice();
		init_info.Device = device->GetVulkanDevice();
		init_info.QueueFamily = physicalDevice->GetQueueFamilyIndices().Graphics;
		init_info.Queue = device->GetGraphicsQueue();
		init_info.PipelineCache = nullptr;
		init_info.DescriptorPool = s_Data->ImGuiDescriptorPool;
		init_info.Allocator = nullptr;
		init_info.MinImageCount = 2;
		Ref<Swapchain> swapchain = Application::Get().GetWindow()->GetSwapchain();
		init_info.ImageCount = swapchain->GetImageCount();
		init_info.CheckVkResultFn = cerberus::VulkanCheckResult;
		ImGui_ImplVulkan_Init(&init_info, swapchain->GetRenderPass());

		// Upload Fonts
		{
			// Use any command queue

			VkCommandBuffer commandBuffer = Renderer::GetCommandBuffer(true);
			ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
			Renderer::FlushCommandBuffer(commandBuffer);

			VK_CHECK_RESULT(vkDeviceWaitIdle(device->GetVulkanDevice()));
			ImGui_ImplVulkan_DestroyFontUploadObjects();
		}

		uint32_t framesInFlight = Renderer::GetRendererConfig().FramesInFlight;
		s_ImGuiCommandBuffers.resize(framesInFlight);
		for (uint32_t i = 0; i < framesInFlight; i++)
			s_ImGuiCommandBuffers[i] = Renderer::CreateSecondaryCommandBuffer();
	}

	void Renderer::ShutdownImGui()
	{
		auto device = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		VK_CHECK_RESULT(vkDeviceWaitIdle(device));
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		vkDestroyDescriptorPool(device, s_Data->ImGuiDescriptorPool, nullptr);
	}

	void Renderer::RenderImGui()
	{
		RenderQueueManager::Submit([]() mutable
		{
			BeginImGui(s_Data->MainCommandBuffer);

			auto& application = Application::Get();
			application.OnImGuiRender();

			EndImGui(s_Data->MainCommandBuffer);
		});
	}

	void Renderer::BeginImGui(CommandBuffer* commandBuffer)
	{
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void Renderer::EndImGui(CommandBuffer* commandBuffer)
	{
		ImGui::Render();

		Swapchain& swapChain = *(Application::Get().GetWindow()->GetSwapchain().get());

		VkClearValue clearValues[2];
		clearValues[0].color = { {0.1f, 0.1f,0.1f, 1.0f} };
		clearValues[1].depthStencil = { 1.0f, 0 };

		uint32_t width = swapChain.GetWidth();
		uint32_t height = swapChain.GetHeight();

		uint32_t commandBufferIndex = swapChain.GetCurrentBufferIndex();

		VkCommandBufferBeginInfo drawCmdBufInfo = {};
		drawCmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		drawCmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		drawCmdBufInfo.pNext = nullptr;

		VkCommandBuffer drawCommandBuffer = swapChain.GetCurrentDrawCommandBuffer();
		VK_CHECK_RESULT(vkBeginCommandBuffer(drawCommandBuffer, &drawCmdBufInfo));

		VkRenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.pNext = nullptr;
		renderPassBeginInfo.renderPass = swapChain.GetRenderPass();
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2; // Color + depth
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = swapChain.GetActiveFramebuffer();

		vkCmdBeginRenderPass(drawCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

		VkCommandBufferInheritanceInfo inheritanceInfo = {};
		inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
		inheritanceInfo.renderPass = swapChain.GetRenderPass();
		inheritanceInfo.framebuffer = swapChain.GetActiveFramebuffer();

		VkCommandBufferBeginInfo cmdBufInfo = {};
		cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
		cmdBufInfo.pInheritanceInfo = &inheritanceInfo;

		VK_CHECK_RESULT(vkBeginCommandBuffer(s_ImGuiCommandBuffers[commandBufferIndex], &cmdBufInfo));

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = (float)height;
		viewport.height = -(float)height;
		viewport.width = (float)width;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(s_ImGuiCommandBuffers[commandBufferIndex], 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.extent.width = width;
		scissor.extent.height = height;
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		vkCmdSetScissor(s_ImGuiCommandBuffers[commandBufferIndex], 0, 1, &scissor);

		ImDrawData* main_draw_data = ImGui::GetDrawData();
		ImGui_ImplVulkan_RenderDrawData(main_draw_data, s_ImGuiCommandBuffers[commandBufferIndex]);

		VK_CHECK_RESULT(vkEndCommandBuffer(s_ImGuiCommandBuffers[commandBufferIndex]));

		std::vector<VkCommandBuffer> commandBuffers;
		commandBuffers.push_back(s_ImGuiCommandBuffers[commandBufferIndex]);

		vkCmdExecuteCommands(drawCommandBuffer, uint32_t(commandBuffers.size()), commandBuffers.data());

		vkCmdEndRenderPass(drawCommandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(drawCommandBuffer));

		ImGuiIO& io = ImGui::GetIO(); (void)io;
		// Update and Render additional Platform Windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
#if 0
		ImGui::Render();

		VkCommandBuffer drawCommandBuffer = CommandBuffer->GetCommandBuffer();

		ImDrawData* main_draw_data = ImGui::GetDrawData();
		ImGui_ImplVulkan_RenderDrawData(main_draw_data, drawCommandBuffer);
		/** /
		vkCmdEndRenderPass(drawCommandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(drawCommandBuffer));
		/**/
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		// Update and Render additional Platform Windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
#endif

	}

	void Renderer::SetImGuiColorTheme()
	{
		auto& style = ImGui::GetStyle();
		auto& colors = ImGui::GetStyle().Colors;

		//========================================================
		/// Colors

		// Headers
		colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(colors::theme::groupHeader);
		colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(colors::theme::groupHeader);
		colors[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(colors::theme::groupHeader);

		// Buttons
		colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(colors::theme::Buttom);
		colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(colors::theme::ButtomHovered);
		colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(colors::theme::ButtomActive);

		// Frame BG
		colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(colors::theme::propertyField);
		colors[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(colors::theme::propertyField);
		colors[ImGuiCol_FrameBgActive] = ImGui::ColorConvertU32ToFloat4(colors::theme::propertyField);

		// Tabs
		colors[ImGuiCol_Tab] = ImGui::ColorConvertU32ToFloat4(colors::theme::titlebar);
		colors[ImGuiCol_TabHovered] = ImGui::ColorConvertU32ToFloat4(colors::theme::titlebarHovered);
		colors[ImGuiCol_TabActive] = ImGui::ColorConvertU32ToFloat4(colors::theme::titlebarActive);
		colors[ImGuiCol_TabUnfocused] = ImGui::ColorConvertU32ToFloat4(colors::theme::titlebar);
		colors[ImGuiCol_TabUnfocusedActive] = colors[ImGuiCol_TabHovered];

		// Title
		colors[ImGuiCol_TitleBg] = ImGui::ColorConvertU32ToFloat4(colors::theme::titlebar);
		colors[ImGuiCol_TitleBgActive] = ImGui::ColorConvertU32ToFloat4(colors::theme::titlebar);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Resize Grip
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);

		// Scrollbar
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.0f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.0f);

		// Check Mark
		colors[ImGuiCol_CheckMark] = ImColor(200, 200, 200, 255);

		// Slider
		colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 0.7f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.66f, 0.66f, 0.66f, 1.0f);

		// Text
		colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(colors::theme::text);

		// Checkbox
		colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(colors::theme::text);

		// Separator
		colors[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(colors::theme::backgroundDark);
		colors[ImGuiCol_SeparatorActive] = ImGui::ColorConvertU32ToFloat4(colors::theme::highlight);
		colors[ImGuiCol_SeparatorHovered] = ImGui::ColorConvertU32ToFloat4(colors::theme::highlightHovered);

		// Window Background
		colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(colors::theme::titlebar);
		colors[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(colors::theme::background);
		colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(colors::theme::backgroundPopup);
		colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(colors::theme::backgroundDark);

		// Tables
		colors[ImGuiCol_TableHeaderBg] = ImGui::ColorConvertU32ToFloat4(colors::theme::groupHeader);
		colors[ImGuiCol_TableBorderLight] = ImGui::ColorConvertU32ToFloat4(colors::theme::backgroundDark);

		// Menubar
		colors[ImGuiCol_MenuBarBg] = ImColor(200, 200, 200, 255);

		//========================================================
		/// Style
		style.FrameRounding = 2.5f;
		style.FrameBorderSize = 1.0f;
		style.IndentSpacing = 11.0f;
	}

	ShaderLibrary& Renderer::GetShaderLibrary()
	{
		return *s_Data->ShaderLibrary.get();
	}
	// Frame -> Set -> Descriptors
	static std::vector<std::vector<std::vector<VkWriteDescriptorSet>>>& RetrieveOrCreateUniformBufferWriteDescriptors(Ref<UniformBufferSet> uniformBufferSet, Ref<Shader> shader)
	{
		size_t shaderHash = shader->GetHash();
#if OLD_RENDERER
		// If the uniform buffer already extis in the map use that write descriptor set
		if (s_Data->UniformBufferWriteDescriptorCache.find(uniformBufferSet.get()) != s_Data->UniformBufferWriteDescriptorCache.end())
		{
			auto& shaderMap = s_Data->UniformBufferWriteDescriptorCache.at(uniformBufferSet.get());
			if (shaderMap.find(shaderHash) != shaderMap.end())
			{
				auto& writeDescriptors = shaderMap.at(shaderHash);
				return writeDescriptors;
			}
		}

		uint32_t framesInFlight = Renderer::GetRendererConfig().FramesInFlight;
		// Check if the Shader has descriptor sets
		if (shader->HasDescriptorSet(0))
		{
			const auto& shaderDescriptorSets = shader->GetShaderDescriptorSets();
			if (!shaderDescriptorSets.empty())
			{
				for (uint32_t i = 0; i < shaderDescriptorSets.size(); i++)
				{
					for (auto&& [binding, shaderUB] : shaderDescriptorSets[i].UniformBuffers)
					{
						// Add this new entry to the descriptor set map
						auto& writeDescriptors = s_Data->UniformBufferWriteDescriptorCache[uniformBufferSet.get()][shaderHash];
						writeDescriptors.resize(framesInFlight);
						for (uint32_t frame = 0; frame < framesInFlight; frame++)
						{
							Ref<UniformBuffer> uniformBuffer = uniformBufferSet->Get(binding, i, frame);
							VkWriteDescriptorSet writeDescriptorSet = {};
							writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
							writeDescriptorSet.descriptorCount = 1;
							writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
							writeDescriptorSet.pBufferInfo = &uniformBuffer->GetBufferDescriptorInfo();
							writeDescriptorSet.dstBinding = uniformBuffer->GetBindingPoint();
							writeDescriptors[frame].resize(shaderDescriptorSets.size());
							writeDescriptors[frame][i].push_back(writeDescriptorSet);
						}
					}
				}

			}
		}
#endif
		return s_Data->UniformBufferWriteDescriptorCache[uniformBufferSet.get()][shaderHash];
	}

	void Renderer::UpdateMaterialForRendering(Ref<Material> material, Ref<UniformBufferSet> uniformBufferSet)
	{
#if OLD_RENDERER
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		auto writeDescriptors = RetrieveOrCreateUniformBufferWriteDescriptors(uniformBufferSet, material->GetShader());
		material->UpdateForRender(writeDescriptors);
#endif
	}

	VkDescriptorSet Renderer::AllocateDescriptorSet(VkDescriptorSetLayout& layout, bool persistent)
	{
		return persistent ? s_Data->PersistentDescriptorAllocator->Allocate(layout) : s_Data->DescriptorAllocator->Allocate(layout);
	}

	Ref<RendererContext> Renderer::GetRenderContext()
	{
		return Application::Get().GetWindow()->GetRendererContext();
	}

	void Renderer::BeginRenderPass(CommandBuffer* commandBuffer, Handle<RenderPass> hdRenderPass, const DrawArea& drawArea)
	{
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		
		Framebuffer* framebuffer = ResourceManager::GetFramebuffer(drawArea.Framebuffer);
		RenderPass* renderPass = ResourceManager::GetRenderPass(hdRenderPass);

		VkCommandBuffer vulkanCmdBuffer = commandBuffer->GetCommandBuffer(frameIndex);
		uint32_t width = framebuffer->GetWidth();
		uint32_t height = framebuffer->GetHeight();
		// Information about how to begin a render pass (only needed for graphical applications)
		VkRenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderPass->GetHandle();				// Render Pass to begin
		renderPassBeginInfo.renderArea.offset = { 0, 0 };						// Start point of render pass in pixels
		renderPassBeginInfo.renderArea.extent = { width, height };				// Size of region to run render pass on (starting at offset)


		// A clear value for each attachment
		// Each element represent an attachement of the renderpass/framebuffer
		// The order is important becuase is the same as the attachments in renderpass/framebuffer
		std::vector< VkClearValue> clearValues = framebuffer->GetVulkanClearValues();
		renderPassBeginInfo.pClearValues = clearValues.data();
		renderPassBeginInfo.clearValueCount = (uint32_t)clearValues.size();
		renderPassBeginInfo.framebuffer = framebuffer->GetHandle();
		// Begin Render Pass
		vkCmdBeginRenderPass(vulkanCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);


		if (drawArea.ExplicitClear)
		{
			const uint32_t colorAttachmentCount = (uint32_t)framebuffer->GetColorAttachmentCount();
			const uint32_t totalAttachmentCount = colorAttachmentCount + (framebuffer->HasDepthAttachment() ? 1 : 0);
			CORE_ASSERT(clearValues.size() == totalAttachmentCount, "");

			std::vector<VkClearAttachment> attachments(totalAttachmentCount);
			std::vector<VkClearRect> clearRects(totalAttachmentCount);
			for (uint32_t i = 0; i < colorAttachmentCount; i++)
			{
				attachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				attachments[i].colorAttachment = i;
				attachments[i].clearValue = clearValues[i];

				clearRects[i].rect.offset = { (int32_t)0, (int32_t)0 };
				clearRects[i].rect.extent = { width, height };
				clearRects[i].baseArrayLayer = 0;
				clearRects[i].layerCount = 1;
			}

			if (framebuffer->HasDepthAttachment())
			{
				attachments[colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				attachments[colorAttachmentCount].clearValue = clearValues[colorAttachmentCount];
				clearRects[colorAttachmentCount].rect.offset = { (int32_t)0, (int32_t)0 };
				clearRects[colorAttachmentCount].rect.extent = { width, height };
				clearRects[colorAttachmentCount].baseArrayLayer = 0;
				clearRects[colorAttachmentCount].layerCount = 1;
			}

			vkCmdClearAttachments(vulkanCmdBuffer, totalAttachmentCount, attachments.data(), totalAttachmentCount, clearRects.data());

		}

		VkViewport viewport = {};
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		viewport.x = 0.f;
		viewport.y = 0.f;
		viewport.width = (float)width;
		viewport.height = (float)height;
		// Update dynamic viewport state
		vkCmdSetViewport(vulkanCmdBuffer, 0, 1, &viewport);


		// Update dynamic scissor state
		VkRect2D scissor = {};
		scissor.extent = { width, height };
		scissor.offset = { 0, 0 };
		vkCmdSetScissor(vulkanCmdBuffer, 0, 1, &scissor);


		// Bind Descriptor Sets
		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(drawArea.GraphicsPipeline);
		CORE_ASSERT(pipeline, "Null graphics pipeline");
		// Bind Pipeline to be used in render pass
		vkCmdBindPipeline(vulkanCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());

		VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
		BindGroup* bindGroup0 = ResourceManager::GetBindGroup(drawArea.BindGroup0);
		BindGroup* bindGroup1 = ResourceManager::GetBindGroup(drawArea.BindGroup1);
		std::array<VkDescriptorSet, 2> descriptorSets =
		{
			bindGroup0->GetHandle(frameIndex),
			bindGroup1->GetHandle(frameIndex)

		};

		// Bind the descriptor set
		vkCmdBindDescriptorSets(vulkanCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, (uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
#if OLD_RENDERER
		Renderer::Submit([CommandBuffer, renderPass, explicitClear]()
		{
			Ref<Framebuffer> framebuffer = renderPass->GetSpecification().TargetFramebuffer;
			uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

			VkCommandBuffer commandBuffer = CommandBuffer->GetCommandBuffer(frameIndex);
			uint32_t width, height;
			if (framebuffer->GetSpecification().SwapchainTarget)
			{
				VkExtent2D swapchainExtend = Application::Get().GetWindow()->GetCurrentSwapchain()->GetSwapchainExtent();
				width = swapchainExtend.width;
				height = swapchainExtend.height;
			}
			else
			{
				width = framebuffer->GetWidth();
				height = framebuffer->GetHeight();
			}
			// Information about how to begin a render pass (only needed for graphical applications)
			VkRenderPassBeginInfo renderPassBeginInfo = {};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = renderPass->GetVulkanRenderPass();			// Render Pass to begin
			renderPassBeginInfo.renderArea.offset = { 0, 0 };							// Start point of render pass in pixels
			renderPassBeginInfo.renderArea.extent = { width, height };					// Size of region to run render pass on (starting at offset)


			// A clear value for each attachment
			// Each element represent an attachement of the renderpass/framebuffer
			// The order is important becuase is the same as the attachments in renderpass/framebuffer
			std::vector< VkClearValue> clearValues = framebuffer->GetVulkanClearValues();
			renderPassBeginInfo.pClearValues = clearValues.data();
			renderPassBeginInfo.clearValueCount = (uint32_t)clearValues.size();
			renderPassBeginInfo.framebuffer = framebuffer->GetVulkanFramebuffer();
			// Begin Render Pass
			vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			

			if (explicitClear)
			{
				const uint32_t colorAttachmentCount = (uint32_t)framebuffer->GetColorAttachmentCount();
				const uint32_t totalAttachmentCount = colorAttachmentCount + (framebuffer->HasDepthAttachment() ? 1 : 0);
				CORE_ASSERT(clearValues.size() == totalAttachmentCount, "");

				std::vector<VkClearAttachment> attachments(totalAttachmentCount);
				std::vector<VkClearRect> clearRects(totalAttachmentCount);
				for (uint32_t i = 0; i < colorAttachmentCount; i++)
				{
					attachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					attachments[i].colorAttachment = i;
					attachments[i].clearValue = clearValues[i];

					clearRects[i].rect.offset = { (int32_t)0, (int32_t)0 };
					clearRects[i].rect.extent = { width, height };
					clearRects[i].baseArrayLayer = 0;
					clearRects[i].layerCount = 1;
				}

				if (framebuffer->HasDepthAttachment())
				{
					attachments[colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
					attachments[colorAttachmentCount].clearValue = clearValues[colorAttachmentCount];
					clearRects[colorAttachmentCount].rect.offset = { (int32_t)0, (int32_t)0 };
					clearRects[colorAttachmentCount].rect.extent = { width, height };
					clearRects[colorAttachmentCount].baseArrayLayer = 0;
					clearRects[colorAttachmentCount].layerCount = 1;
				}

				vkCmdClearAttachments(commandBuffer, totalAttachmentCount, attachments.data(), totalAttachmentCount, clearRects.data());

			}

			VkViewport viewport = {};
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			viewport.x = 0.f;
			viewport.y = framebuffer->GetSpecification().SwapchainTarget ? 0.f : 0;
			viewport.width = (float)width;
			viewport.height = framebuffer->GetSpecification().SwapchainTarget ? (float)height : (float)height;
			// Update dynamic viewport state
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);


			// Update dynamic scissor state
			VkRect2D scissor = {};
			scissor.extent = { width, height };
			scissor.offset = { 0, 0 };
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		});
#endif

	}

	void Renderer::EndRenderPass(CommandBuffer* commandBuffer)
	{
		// End Render Pass
		vkCmdEndRenderPass(commandBuffer->GetCommandBuffer(Renderer::GetCurrentFrameIndex()));

		s_Data->DrawCallCount = 0;
#if OLD_RENDERER
		Renderer::Submit([CommandBuffer]()
		{
			// End Render Pass
			vkCmdEndRenderPass(CommandBuffer->GetCommandBuffer(Renderer::GetCurrentFrameIndex()));

			s_Data->DrawCallCount = 0;
		});
#endif
	}

	void Renderer::SetActiveVertexAndIndexBuffer(CommandBuffer* commandBuffer, const Handle<GPUBuffer> hVertexBuffer, const Handle<GPUBuffer> hInderBuffer)
	{
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		VkCommandBuffer vulkanCmdBuffer = commandBuffer->GetCommandBuffer(frameIndex);

		// Index Buffer
		GPUBuffer* indexBuffer = ResourceManager::GetBuffer(hInderBuffer);
		CORE_ASSERT(indexBuffer, "Null GPU index buffer");
		// VK_INDEX_TYPE_UINT32 : The type of data that index buffer stores
		// Bind index buffer
		vkCmdBindIndexBuffer(vulkanCmdBuffer, indexBuffer->Buffer, 0, VK_INDEX_TYPE_UINT32);
		
		// Vertex Buffer
		GPUBuffer* vertexBuffer = ResourceManager::GetBuffer(hVertexBuffer);
		CORE_ASSERT(vertexBuffer, "Null GPU vertex buffer");
		VkDeviceSize offsets[1] = { 0 };
		// Bind vertex buffer
		vkCmdBindVertexBuffers(vulkanCmdBuffer, 0, 1, &vertexBuffer->Buffer, offsets);
	}

	VkCommandBuffer Renderer::GetCommandBuffer(bool beginCommand, bool compute)
	{
		VkCommandBuffer cmdBuffer;

		VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
		cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdBufAllocateInfo.commandPool = compute ? s_Data->ComputeCommandPool : s_Data->CommandPool;
		cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdBufAllocateInfo.commandBufferCount = 1;

		auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		// Allocate command buffer from the queue
		VK_CHECK_RESULT(vkAllocateCommandBuffers(vulkanDevice, &cmdBufAllocateInfo, &cmdBuffer));
		
		if (beginCommand)
		{
			VkCommandBufferBeginInfo cmdBufferBeginInfo{};
			cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			// VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT =  each recording of the command buffer will only be submitted once, 
			// and the command buffer will be reset and recorded again between each submission.
			// TODO: Check if this needed
			cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo));
		}
		return cmdBuffer;
	}

	CommandBuffer* Renderer::GetMainCommandBuffer(bool swapchain)
	{
		if(swapchain)
			return s_Data->SwapchainCommandBuffer;
		else
			return s_Data->MainCommandBuffer;
	}

	VkCommandBuffer Renderer::CreateSecondaryCommandBuffer()
	{
		VkCommandBuffer cmdBuffer;

		VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
		cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdBufAllocateInfo.commandPool = s_Data->CommandPool;
		cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
		cmdBufAllocateInfo.commandBufferCount = 1;

		VK_CHECK_RESULT(vkAllocateCommandBuffers(RendererContext::GetCurrentDevice()->GetVulkanDevice(), &cmdBufAllocateInfo, &cmdBuffer));

		return cmdBuffer;
	}

	void Renderer::FlushCommandBuffer(VkCommandBuffer commandBuffer, bool compute, bool waitToCompute, bool detroyCommandBuffer)
	{
		CORE_ASSERT(commandBuffer != VK_NULL_HANDLE, "Null command buffer");

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;


		auto device = RendererContext::GetCurrentDevice();
		auto vulkanDevice = device->GetVulkanDevice();
		// Create fence to ensure that the command buffer has finished executing
		VkFence fence;
		VkFenceCreateInfo fenceCreateInfo = {};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.flags = 0;
		VK_CHECK_RESULT(vkCreateFence(vulkanDevice, &fenceCreateInfo, nullptr, &fence));

		// Submit command buffer to queue
		VkQueue queue = compute ? device->GetComputeQueue() : device->GetGraphicsQueue();
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
		// Wait for the fence to signal that command buffer has finished executing
		VK_CHECK_RESULT(vkWaitForFences(vulkanDevice, 1, &fence, VK_TRUE, UINT64_MAX));

		if (waitToCompute)
			VK_CHECK_RESULT(vkQueueWaitIdle(queue));

		vkDestroyFence(vulkanDevice, fence, nullptr);

		if (detroyCommandBuffer)
		{
			VkCommandPool commandPool = compute ? s_Data->ComputeCommandPool : s_Data->CommandPool;
			vkFreeCommandBuffers(vulkanDevice, commandPool, 1, &commandBuffer);
		}
	}

	RendererConfig& Renderer::GetRendererConfig()
	{
		static RendererConfig config;
		return config;
	}

	Ref<EnvironmentMap> Renderer::CreateEnvironmentMap(const std::string& filepath)
	{
#if 0
		// Tiering settings
		const uint32_t environmentMapResolution = 1024;
		const uint32_t irradianceMapComputeSamples = 1024;
		const uint32_t irradianceMapSize = 32;
		////////////////////////////////////////////
		// Convert equirectangular to cubemap
		////////////////////////////////////////////
		TextureProperties envEquirectProperties;
		Ref<Texture2D> envEquirect = CreateRef<Texture2D>(filepath, envEquirectProperties);
		CORE_ASSERT(envEquirect->GetFormat() == ImageFormat::RGBA32F, "Texture is not HDR!");
		TextureProperties properties;
		properties.DebugName = "Environment Map Unfiltered";
		Ref<TextureCube> envUnfiltered = CreateRef<TextureCube>(ImageFormat::RGBA32F, environmentMapResolution, environmentMapResolution, properties);
		{
			// Get compute shader and create pipeline
			Ref<Shader> equirectangularConversionShader = Renderer::GetShaderLibrary().Get("EquirectangularToCubeMap");
			Ref<ComputePipeline> equirectangularConversionPipeline = CreateRef<ComputePipeline>(equirectangularConversionShader);
			Renderer::Submit([equirectangularConversionPipeline, envEquirect, envUnfiltered, environmentMapResolution]()
			{
				VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
				Ref<Shader> shader = equirectangularConversionPipeline->GetShader();
				// Descriptors
				std::array<VkWriteDescriptorSet, 2> writeDescriptors;
				VkDescriptorSet descriptorSet = Renderer::AllocateDescriptorSet(shader->GetDescriptorSetLayout(0));
				// Set descriptor for the Output Cubemap
				writeDescriptors[0] = *shader->GetWriteDescriptorSet("o_CubeMap", 0);
				writeDescriptors[0].dstSet = descriptorSet;
				writeDescriptors[0].pImageInfo = &envUnfiltered->GetVulkanDescriptorInfo();
				// Set descriptor for the Input Equirectangular texture2D
				writeDescriptors[1] = *shader->GetWriteDescriptorSet("u_EquirectangularTex", 0);
				writeDescriptors[1].dstSet = descriptorSet;
				writeDescriptors[1].pImageInfo = &envEquirect->GetVulkanDescriptorInfo();
				// Update descriptors
				vkUpdateDescriptorSets(device, (uint32_t)writeDescriptors.size(), writeDescriptors.data(), 0, NULL);
				// Execute Compute Shader
				equirectangularConversionPipeline->Begin();
				{
					uint32_t wordGroupXY = environmentMapResolution / 32;
					equirectangularConversionPipeline->Dispatch(wordGroupXY, wordGroupXY, 6, descriptorSet);
				}
				equirectangularConversionPipeline->End();
				// Generate Mips
				envUnfiltered->GenerateMips(true);
			});
		}
		////////////////////////////////////////////
		// Filter specular environment map
		////////////////////////////////////////////
		properties.DebugName = "Environment Map Filtered";
		Ref<TextureCube> envFiltered = CreateRef<TextureCube>(ImageFormat::RGBA32F, environmentMapResolution, environmentMapResolution, properties);
		{
			// Get compute shader and create pipeline
			Ref<Shader> environmentFilteringShader = Renderer::GetShaderLibrary().Get("EnvironmentMipFilter");
			Ref<ComputePipeline> enviromentMipFilteringPipeline = CreateRef<ComputePipeline>(environmentFilteringShader);
			Renderer::Submit([enviromentMipFilteringPipeline, envFiltered, envUnfiltered, environmentMapResolution]()
			{
				VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
				Ref<Shader> shader = enviromentMipFilteringPipeline->GetShader();
				uint32_t mipCount = envFiltered->GetMipLevelCount();
				// Images infos
				VkDescriptorImageInfo filteredImageInfo = envFiltered->GetVulkanDescriptorInfo();
				VkDescriptorImageInfo unfilteredImageInfo = envUnfiltered->GetVulkanDescriptorInfo();
				
				std::vector<VkWriteDescriptorSet> writeDescriptors(mipCount * 2);
				std::vector<VkDescriptorImageInfo> mipImageInfos(mipCount);
				std::vector<VkDescriptorSet> descriptorSets(mipCount);


				for (uint32_t i = 0; i < mipCount; i++)
				{
					// Allocate descriptor
					descriptorSets[i] = Renderer::AllocateDescriptorSet(shader->GetDescriptorSetLayout(0));

					VkDescriptorImageInfo& mipImageInfo = mipImageInfos[i];
					mipImageInfo = filteredImageInfo;
					mipImageInfo.imageView = envFiltered->CreateImageViewSingleMip(i);
					// Write decriptor related to mip of the texture to filter
					writeDescriptors[i * 2 + 0] = *shader->GetWriteDescriptorSet("outputTexture", 0);
					writeDescriptors[i * 2 + 0].dstSet = descriptorSets[i];
					writeDescriptors[i * 2 + 0].pImageInfo = &mipImageInfo;
					// Write decriptor of unfiltered texture
					writeDescriptors[i * 2 + 1] = *shader->GetWriteDescriptorSet("inputTexture", 0);
					writeDescriptors[i * 2 + 1].dstSet = descriptorSets[i];
					writeDescriptors[i * 2 + 1].pImageInfo = &unfilteredImageInfo;
				}
				// Update descriptors
				vkUpdateDescriptorSets(device, (uint32_t)writeDescriptors.size(), writeDescriptors.data(), 0, NULL);

				// Compute Pass
				enviromentMipFilteringPipeline->Begin();
				{
					// Pre-filter rest of the mip-chain
					float deltaRoughness = 1.0f / glm::max((float)mipCount - 1.0f, 1.0f);
					for (uint32_t i = 0, size = environmentMapResolution; i < mipCount; i++, size /= 2)
					{
						uint32_t numGroups = glm::max(1u, (uint32_t)(size / 32));
						float roughness = i * deltaRoughness;
						roughness = glm::max(roughness, 0.05f);
						enviromentMipFilteringPipeline->SetPushConstants(&roughness, sizeof(float));
						// Sent the descriptor set of each mip invidually in order to render into it
						enviromentMipFilteringPipeline->Dispatch(numGroups, numGroups, 6, descriptorSets[i]);
					}
				}
				enviromentMipFilteringPipeline->End();
				// Image View Per Mip
				for (auto& mip : mipImageInfos)
					vkDestroyImageView(device, mip.imageView, nullptr);
			});
		}
		////////////////////////////////////////////
		// Diffuse irradiance environment map
		////////////////////////////////////////////
		properties.DebugName = "Environment Map Irradiance";
		Ref<TextureCube> envIrradiance = CreateRef<TextureCube>(ImageFormat::RGBA32F, irradianceMapSize, irradianceMapSize, properties);
		{
			// Get compute shader and create pipeline
			Ref<Shader> environmentIrradianceShader = Renderer::GetShaderLibrary().Get("EnvironmentIrradiance");
			Ref<ComputePipeline> enviromentIrradiancePipeline = CreateRef<ComputePipeline>(environmentIrradianceShader);
			Renderer::Submit([enviromentIrradiancePipeline, envIrradiance, envFiltered, irradianceMapSize, irradianceMapComputeSamples]()
			{
				VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
				Ref<Shader> shader = enviromentIrradiancePipeline->GetShader();

				// Descriptors
				std::array<VkWriteDescriptorSet, 2> writeDescriptors;
				VkDescriptorSet descriptorSet = Renderer::AllocateDescriptorSet(shader->GetDescriptorSetLayout(0));
				// Set descriptor for the Output Cubemap
				writeDescriptors[0] = *shader->GetWriteDescriptorSet("o_IrradianceMap", 0);
				writeDescriptors[0].dstSet = descriptorSet;
				writeDescriptors[0].pImageInfo = &envIrradiance->GetVulkanDescriptorInfo();
				// Set descriptor for the Input Equirectangular texture2D
				writeDescriptors[1] = *shader->GetWriteDescriptorSet("u_RadianceMap", 0);
				writeDescriptors[1].dstSet = descriptorSet;
				writeDescriptors[1].pImageInfo = &envFiltered->GetVulkanDescriptorInfo();
				// Update descriptors
				vkUpdateDescriptorSets(device, (uint32_t)writeDescriptors.size(), writeDescriptors.data(), 0, NULL);
				
				// Compute Pass
				enviromentIrradiancePipeline->Begin();
				{
					enviromentIrradiancePipeline->SetPushConstants(&irradianceMapComputeSamples, sizeof(uint32_t));
					enviromentIrradiancePipeline->Dispatch(irradianceMapSize / 32, irradianceMapSize / 32, 6, descriptorSet);
				}
				enviromentIrradiancePipeline->End();
				envIrradiance->GenerateMips(true);
			});
		}

		Ref<EnvironmentMap> environment = CreateRef<EnvironmentMap>();
		environment->EquirectangularTex = envEquirect;
		environment->RadianceMap = envFiltered;
		environment->IrradianceMap = envIrradiance;
		environment->DebugName = utils::GetFilename(filepath);
		return environment;
#endif

		return nullptr;
	}

	void Renderer::SetSceneEnvironment(SceneRenderer* sceneRenderer, const Ref<EnvironmentMap>& environment, const Ref<Image>& shadowImage)
	{
#if OLD_RENDERER
		Renderer::Submit([sceneRenderer, environment, shadowImage]()
		{
			Ref<Shader> pbrShader = Renderer::GetShaderLibrary().Get("PBR");
			const uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			// Change to a persistent pool
			if (s_Data->RendererDescriptorSetCache.find(sceneRenderer) == s_Data->RendererDescriptorSetCache.end())
			{
				const uint32_t framesInFlight = Renderer::GetRendererConfig().FramesInFlight;
				s_Data->RendererDescriptorSetCache[sceneRenderer].resize(framesInFlight);
				for (uint32_t i = 0; i < framesInFlight; i++)
				{
					VkDescriptorSet descriptorSet = Renderer::AllocateDescriptorSet(pbrShader->GetDescriptorSetLayout(1), true);
					s_Data->RendererDescriptorSetCache.at(sceneRenderer)[i].push_back(descriptorSet);
				}
					
			}
			VkDescriptorSet descriptorSet = s_Data->RendererDescriptorSetCache.at(sceneRenderer)[currentFrame][0];
			// Dynamic pool allocation
			// VkDescriptorSet descriptorSet = Renderer::AllocateDescriptorSet(pbrShader->GetDescriptorSetLayout(1));
			s_Data->ActiveRendererDescriptorSet = descriptorSet;

			std::array<VkWriteDescriptorSet, 4> writeDescriptors;

			Ref<TextureCube> radianceMap = environment->RadianceMap;
			Ref<TextureCube> irradianceMap = environment->IrradianceMap;
			// Environment Radiance Texture
			writeDescriptors[0] = *pbrShader->GetWriteDescriptorSet("u_EnvRadianceTex", 1);
			writeDescriptors[0].dstSet = descriptorSet;
			const auto& radianceMapImageInfo = radianceMap->GetVulkanDescriptorInfo();
			writeDescriptors[0].pImageInfo = &radianceMapImageInfo;
			// Environment Irradiance Texture
			writeDescriptors[1] = *pbrShader->GetWriteDescriptorSet("u_EnvIrradianceTex", 1);
			writeDescriptors[1].dstSet = descriptorSet;
			const auto& irradianceMapImageInfo = irradianceMap->GetVulkanDescriptorInfo();
			writeDescriptors[1].pImageInfo = &irradianceMapImageInfo;
			// BRDF LUT Texture
			writeDescriptors[2] = *pbrShader->GetWriteDescriptorSet("u_BRDFLUTTex", 1);
			writeDescriptors[2].dstSet = descriptorSet;
			const auto& BrdfLutImageInfo= s_Data->BrdfLutTexture->GetVulkanDescriptorInfo();
			writeDescriptors[2].pImageInfo = &BrdfLutImageInfo;
			// Shadow Map
			// Environment Radiance Texture
			writeDescriptors[3] = *pbrShader->GetWriteDescriptorSet("u_ShadowMapTex", 1);
			writeDescriptors[3].dstSet = descriptorSet;
			const auto& shadowImageInfo = shadowImage->GetVulkanDescriptorInfo();
			writeDescriptors[3].pImageInfo = &shadowImageInfo;
			// Update descriptor
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			vkUpdateDescriptorSets(vulkanDevice, (uint32_t)writeDescriptors.size(), writeDescriptors.data(), 0, nullptr);
		});
#endif
	}

	uint32_t Renderer::GetCurrentFrameIndex()
	{
		return Window::GetCurrentSwapchain()->GetCurrentBufferIndex();
	}



	void Renderer::RenderFullscreenQuad(CommandBuffer* commandBuffer, Handle<GraphicsPipeline> hdPipeline, Handle<BindGroup> hdBindGroup)
	{
		uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
		VkCommandBuffer vulkanCommandBuffer = commandBuffer->GetCommandBuffer(currentFrame);

		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(hdPipeline);

		// Bind Pipeline to be used in render pass
		vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());

		
		// Descriptor Sets
		BindGroup* bindGroup = ResourceManager::GetBindGroup(hdBindGroup);
		if (bindGroup)
		{
			bindGroup->Update(currentFrame);
			VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
			VkDescriptorSet descriptorSets = bindGroup->GetHandle(currentFrame);
			vkCmdBindDescriptorSets(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets, 0, nullptr);
		}
		// Bind Material push constant (Fragment)
		/** /
		Buffer materialPushConstantBuffer = material->GetUniformBufferStorage();
		if (materialPushConstantBuffer.Size)
			vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, materialPushConstantBuffer.Size, materialPushConstantBuffer.Data);
		/**/
		vkCmdDraw(vulkanCommandBuffer, 3, 1, 0, 0);

#if OLD_RENDERER
		Renderer::Submit([CommandBuffer, uniformBufferSet, pipeline, material]() mutable
		{
			uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			VkCommandBuffer vulkanCommandBuffer = CommandBuffer->GetCommandBuffer(currentFrame);
			// Bind Pipeline to be used in render pass
			vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetVulkanPipeline());

			// Update Material
			Renderer::UpdateMaterialForRendering(material, uniformBufferSet);
			// Descriptor Sets
			VkDescriptorSet descriptorSets = material->GetDescriptorSet(currentFrame);
			VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
			// Bind the descriptor
			if(descriptorSets)
				vkCmdBindDescriptorSets(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets, 0, nullptr);

			// Bind Material push constant (Fragment)
			Buffer materialPushConstantBuffer = material->GetUniformBufferStorage();
			if (materialPushConstantBuffer.Size)
				vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, materialPushConstantBuffer.Size, materialPushConstantBuffer.Data);
			
			vkCmdDraw(vulkanCommandBuffer, 3, 1, 0, 0);
		});
#endif
	}

	void Renderer::RenderRenderPass(CommandBuffer* commandBuffer, const DrawArea& drawArea, std::vector<uint32_t> drawStream)
	{
		uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
		VkCommandBuffer vulkanCommandBuffer = commandBuffer->GetCommandBuffer(currentFrame);

		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(drawArea.GraphicsPipeline);
		CORE_ASSERT(pipeline, "Null graphics pipeline");
		// Bind Pipeline to be used in render pass
		vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());

		uint32_t* stream = drawStream.data();
		DrawCallInfo drawCallInfo;
		for (uint32_t index = 0; index < drawArea.DrawCount; index++)
		{
			uint16_t dirtyFlags = (uint16_t)*stream;
			uint32_t dirtyCount = utils::GetCountOfBits(dirtyFlags);

			stream += sizeof(uint32_t);
			if (dirtyFlags & DrawStreamFlags::VERTEX_BUFFER)
			{
				drawCallInfo.VertexBuffer = *(Handle<GPUBuffer>*)&stream;
				stream += sizeof(uint32_t);

				GPUBuffer* vertexBuffer = ResourceManager::GetBuffer(drawCallInfo.VertexBuffer);
				CORE_ASSERT(vertexBuffer, "Null GPU vertex buffer");
				VkDeviceSize offsets[1] = { 0 };
				// Bind vertex buffer
				vkCmdBindVertexBuffers(vulkanCommandBuffer, 0, 1, &vertexBuffer->Buffer, offsets);
			}
			if (dirtyFlags & DrawStreamFlags::INDEX_BUFFER)
			{
				drawCallInfo.IndexBuffer = *(Handle<GPUBuffer>*)&stream;
				stream += sizeof(uint32_t);

				GPUBuffer* indexBuffer = ResourceManager::GetBuffer(drawCallInfo.IndexBuffer);
				CORE_ASSERT(indexBuffer, "Null GPU index buffer");
				VkDeviceSize offsets[1] = { 0 };
				// VK_INDEX_TYPE_UINT32 : The type of data that index buffer stores
				// Bind index buffer
				vkCmdBindIndexBuffer(vulkanCommandBuffer, indexBuffer->Buffer, 0, VK_INDEX_TYPE_UINT32);
			}


			if (dirtyFlags & DrawStreamFlags::VERTEX_OFFSET)
			{
				drawCallInfo.VertexOffset = *stream;
				stream += sizeof(uint32_t);
			}
			if (dirtyFlags & DrawStreamFlags::INDEX_OFFSET)
			{
				drawCallInfo.IndexOffset = *stream;
				stream += sizeof(uint32_t);
			}
			
			if (dirtyFlags & DrawStreamFlags::INDEX_COUNT) 
			{
				drawCallInfo.IndexCount = *stream;
				stream += sizeof(uint32_t);
			}

			// Draw call
			vkCmdDrawIndexed(vulkanCommandBuffer, drawCallInfo.IndexCount, 1, drawCallInfo.IndexOffset, drawCallInfo.VertexOffset, 0);
			s_Data->DrawCallCount++;

		}

	}

	void Renderer::RenderMeshInstances(CommandBuffer* commandBuffer, const DrawArea& drawArea, std::vector<Handle<MeshInstanceProxy>> drawStream)
	{
		uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
		VkCommandBuffer vulkanCommandBuffer = commandBuffer->GetCommandBuffer(currentFrame);
#if 0
		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(drawArea.GraphicsPipeline);
		CORE_ASSERT(pipeline, "Null graphics pipeline");
		// Bind Pipeline to be used in render pass
		vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());


		CORE_ASSERT(drawStream.size(), "Empty draw stream");
		MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(drawStream[0]);
		MeshSourceProxy* meshSource = ResourceManager::GetMeshSourceProxy(meshInstance->MeshSource);

		VkDeviceSize offsets[1] = { 0 };
		VkBuffer vertexBuffer = meshSource->VertexBuffer->GetHandle();
		// Bind vertex buffer
		vkCmdBindVertexBuffers(vulkanCommandBuffer, 0, 1, &vertexBuffer, offsets);


		VkBuffer indexBuffer = meshSource->IndexBuffer->GetHandle();
		// Bind vertex buffer
		vkCmdBindIndexBuffer(vulkanCommandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		// Bind Descriptor Sets
		VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
		BindGroup* bindGroup0 = ResourceManager::GetBindGroup(drawArea.BindGroup0);
		BindGroup* bindGroup1 = ResourceManager::GetBindGroup(drawArea.BindGroup1);
		std::array<VkDescriptorSet, 2> descriptorSets =
		{
			bindGroup0->GetHandle(currentFrame),
			bindGroup1->GetHandle(currentFrame)

		};

		// Bind the descriptor set
		vkCmdBindDescriptorSets(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, (uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
		
		CORE_ASSERT(drawStream.size(), "Empty draw stream");
#endif	
		MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(drawStream[0]);
		MeshSourceProxy* meshSource = ResourceManager::GetMeshSourceProxy(meshInstance->MeshSource);
		SubmeshSourceProxy submeshSource = meshSource->Submeshes[meshInstance->SubmeshIndex];

		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(drawArea.GraphicsPipeline);
		VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();

		for (uint32_t index = 0; index < drawStream.size(); index++)
		{
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(drawStream[index]);
			// Bind Transform push constant (Vertex)
			glm::mat4 worldTransform = meshInstance->WorldTransform;
			vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &worldTransform);

			// Draw call
			vkCmdDrawIndexed(vulkanCommandBuffer, submeshSource.IndexCount, 1, submeshSource.BaseIndex, submeshSource.BaseVertex, 0);
			s_Data->DrawCallCount++;

		}

	}

	/** /
	void Renderer::RenderMesh(Ref<CommandBuffer> CommandBuffer, Ref<UniformBufferSet> uniformBufferSet, Ref<GraphicsPipeline> pipeline, Ref<Mesh> mesh, Ref<MaterialTable> materialTable, glm::mat4& transform)
	{
		Renderer::Submit([CommandBuffer, uniformBufferSet, pipeline, mesh, materialTable, transform]() mutable
		{
#if OLD_RENDERER
			Ref<MeshSource> meshSource = mesh->GetMeshSource();
			uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			VkCommandBuffer vulkanCommandBuffer = CommandBuffer->GetCommandBuffer(currentFrame);
			// Bind Pipeline to be used in render pass
			vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetVulkanPipeline());

			VkBuffer vbMeshBuffer = meshSource->GetVertexBuffer()->GetVulkanBuffer();
			VkDeviceSize offsets[1] = { 0 };
			// Bind vertex buffer
			vkCmdBindVertexBuffers(vulkanCommandBuffer, 0, 1, &vbMeshBuffer, offsets);

			VkBuffer ibMeshBuffer = meshSource->GetIndexBuffer()->GetVulkanBuffer();
			// VK_INDEX_TYPE_UINT32 : The type of data that index buffer stores
			// Bind index buffer
			vkCmdBindIndexBuffer(vulkanCommandBuffer, ibMeshBuffer, 0, VK_INDEX_TYPE_UINT32);

			auto& subMeshes = meshSource->GetSubmeshes();
			Ref<MaterialTable> materialTableMesh = materialTable;
			// Update Descripor set per material
			for (uint32_t i = 0; i < materialTableMesh->GetMaterialCount(); i++)
			{
				Ref<MaterialAsset> materialAsset = materialTableMesh->GetMaterial(i);
				Renderer::UpdateMaterialForRendering(materialAsset->GetMaterial(), uniformBufferSet);
			}

			for (uint32_t index = 0; index < subMeshes.size(); index++)
			{
				// Update Material
				const Submesh& submesh = subMeshes[index];
				Ref<Material> material = materialTableMesh->GetMaterial(submesh.MaterialIndex)->GetMaterial();
				// Bind Descriptor Sets
				VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
				std::array<VkDescriptorSet, 2> descriptorSets = 
				{	
					material->GetDescriptorSet(currentFrame),		// Set 0
					s_Data->ActiveRendererDescriptorSet						// Set 1 (Environment)
				};

				// Bind the descriptor set
				vkCmdBindDescriptorSets(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
					0, (uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);

				// Bind Transform push constant (Vertex)
				glm::mat4 worldTransform = transform * submesh.Transform;
				vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &worldTransform);
				// Bind Material push constant (Fragment)
				Buffer materialPushConstantBuffer = material->GetUniformBufferStorage();
				vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4), materialPushConstantBuffer.Size, materialPushConstantBuffer.Data);
					
				// Draw call
				vkCmdDrawIndexed(vulkanCommandBuffer, submesh.IndexCount, 1, submesh.BaseIndex, submesh.BaseVertex, 0);
				s_Data->DrawCallCount++;
			}
#endif
		});
	}

	void Renderer::RenderMeshWithMaterial(Ref<CommandBuffer> CommandBuffer, Ref<UniformBufferSet> uniformBufferSet, Ref<Pipeline> pipeline, Ref<Mesh> mesh, glm::mat4& transform, Ref<Material> material, const Buffer& additionalUniforms)
	{
#if OLD_RENDERER
		Buffer pushConstantBuffer;
		pushConstantBuffer.Allocate(sizeof(glm::mat4) + additionalUniforms.Size);
		if (additionalUniforms.Size)
			pushConstantBuffer.Write(additionalUniforms.Data, additionalUniforms.Size, sizeof(glm::mat4));

		Renderer::Submit([CommandBuffer, uniformBufferSet, pipeline, mesh, transform, material, pushConstantBuffer]() mutable
		{
			Ref<MeshSource> meshSource = mesh->GetMeshSource();
			uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			VkCommandBuffer vulkanCommandBuffer = CommandBuffer->GetCommandBuffer(currentFrame);
			// Bind Pipeline to be used in render pass
			vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetVulkanPipeline());

			VkBuffer vbMeshBuffer = meshSource->GetVertexBuffer()->GetVulkanBuffer();
			VkDeviceSize offsets[1] = { 0 };
			// Bind vertex buffer
			vkCmdBindVertexBuffers(vulkanCommandBuffer, 0, 1, &vbMeshBuffer, offsets);

			VkBuffer ibMeshBuffer = meshSource->GetIndexBuffer()->GetVulkanBuffer();
			// VK_INDEX_TYPE_UINT32 : The type of data that index buffer stores
			// Bind index buffer
			vkCmdBindIndexBuffer(vulkanCommandBuffer, ibMeshBuffer, 0, VK_INDEX_TYPE_UINT32);

			
			auto& subMeshes = meshSource->GetSubmeshes();
			// Update Descripor set per material
			Renderer::UpdateMaterialForRendering(material, uniformBufferSet);

			// Bind Descriptor Sets
			VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
			VkDescriptorSet descriptorSet = material->GetDescriptorSet(currentFrame);

			// Bind the descriptor set
			if (descriptorSet)
				vkCmdBindDescriptorSets(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
					0, 1, &descriptorSet, 0, nullptr);
			// Bind Material push constant (Fragment)
			Buffer materialPushConstantBuffer = material->GetUniformBufferStorage();
			if (materialPushConstantBuffer)
				vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, pushConstantBuffer.Size, materialPushConstantBuffer.Size, materialPushConstantBuffer.Data);

			for (uint32_t index = 0; index < subMeshes.size(); index++)
			{
				// Update Material
				const Submesh& submesh = subMeshes[index];
				// Bind Transform push constant (Vertex)
				glm::mat4 worldTransform = transform * submesh.Transform;
				pushConstantBuffer.Write(&worldTransform, sizeof(glm::mat4), 0);
				vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, pushConstantBuffer.Size, pushConstantBuffer.Data);
				// Draw call
				vkCmdDrawIndexed(vulkanCommandBuffer, submesh.IndexCount, 1, submesh.BaseIndex, submesh.BaseVertex, 0);
				s_Data->DrawCallCount++;
			}
			pushConstantBuffer.Release();
		});

#endif
	}
	/**/
	void Renderer::DispatchComputeShader(Ref<CommandBuffer> CommandBuffer, Ref<ComputePipeline> pipeline, Ref<UniformBufferSet> uniformBufferSet, Ref<Material> material, const glm::ivec3& workGroups, Buffer pushConstantBuffer)
	{
#if OLD_RENDERER
		Renderer::Submit([CommandBuffer, uniformBufferSet, pipeline, workGroups, material, pushConstantBuffer]() mutable
		{
			uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			// Update Descripor set per material
			Renderer::UpdateMaterialForRendering(material, uniformBufferSet);
			VkDescriptorSet descriptorSet = material->GetDescriptorSet(currentFrame);
			pipeline->Begin(CommandBuffer);
			{
				if (pushConstantBuffer.Size)
					pipeline->SetPushConstants(pushConstantBuffer.Data, pushConstantBuffer.Size);

				pipeline->Dispatch(workGroups.x, workGroups.y, workGroups.z, descriptorSet);
			}
			pipeline->End();

		});
#endif
	}

	VkPhysicalDeviceLimits Renderer::GetDeviceLimits()
	{
		return s_Data->DeviceLimits;
	}

	Handle<Texture2D> Renderer::GetWhiteTexture()
	{
		return s_Data->WhiteTexture;
	}

	Ref<MaterialAsset> Renderer::GetDefaultPBRMaterial()
	{
		return s_Data->DefaultMaterial;
	}

	Ref<EnvironmentMap> Renderer::GetEmptyEnvironmentMap()
	{
		return s_Data->EmptyEnvironment;
	}

	VkDescriptorSetLayout Renderer::CreateDescriptorSetLayout(VkDescriptorSetLayoutCreateInfo* info)
	{
		return s_Data->DescriptorLayoutCache->CreateDescriptorLayout(info);
	}
}