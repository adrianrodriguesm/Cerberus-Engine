#include "pch.h"
#include "ImGuiRenderer.h"

#include "Core/Application.h"
#include "Renderer/Renderer.h"
#include "Renderer/RendererContext.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/Swapchain.h"

#include "ImGui/ImGuizmo.h"
#include "ImGui/ImGuiColors.h"

#include "imgui.h"
#ifndef IMGUI_IMPL_API
#define IMGUI_IMPL_API
#endif
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan_textures.h"

namespace cerberus
{
	struct ImGuiRendererData
	{
		VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> SecondaryCommandBuffers;
	};

	static ImGuiRendererData* s_ImGuiData = nullptr;

	void ImGuiRenderer::Init(CommandPool& graphicsCommandPool)
	{
		CORE_ASSERT(!s_ImGuiData, "ImGuiRenderer already initialized");
		s_ImGuiData = new ImGuiRendererData();

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
		SetColorTheme();

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
		VK_CHECK_RESULT(vkCreateDescriptorPool(device->GetVulkanDevice(), &pool_info, nullptr, &s_ImGuiData->DescriptorPool));

		// Setup Platform/Renderer bindings
		ImGui_ImplGlfw_InitForVulkan(window, true);
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = RendererContext::GetInstance();
		init_info.PhysicalDevice = physicalDevice->GetVulkanPhysicalDevice();
		init_info.Device = device->GetVulkanDevice();
		init_info.QueueFamily = physicalDevice->GetQueueFamilyIndices().Graphics;
		init_info.Queue = device->GetGraphicsQueue();
		init_info.PipelineCache = nullptr;
		init_info.DescriptorPool = s_ImGuiData->DescriptorPool;
		init_info.Allocator = nullptr;
		init_info.MinImageCount = 2;
		Ref<Swapchain> swapchain = Application::Get().GetWindow()->GetSwapchain();
		init_info.ImageCount = swapchain->GetImageCount();
		init_info.CheckVkResultFn = cerberus::VulkanCheckResult;
		ImGui_ImplVulkan_Init(&init_info, swapchain->GetRenderPass());

		// Upload Fonts
		{
			TransientCommandBuffer commandBuffer = Renderer::GetTransientCommandExecutor().BeginGraphics();
			// Dear ImGui is an external Vulkan backend and therefore remains an
			// explicit native-handle boundary.
			ImGui_ImplVulkan_CreateFontsTexture(commandBuffer.GetNative());
			commandBuffer.SubmitAndWait();

			VK_CHECK_RESULT(vkDeviceWaitIdle(device->GetVulkanDevice()));
			ImGui_ImplVulkan_DestroyFontUploadObjects();
		}
		
		uint32_t framesInFlight = Renderer::GetRendererConfig().FramesInFlight;
		s_ImGuiData->SecondaryCommandBuffers.resize(framesInFlight);
		for (uint32_t i = 0; i < framesInFlight; i++)
			s_ImGuiData->SecondaryCommandBuffers[i] = graphicsCommandPool.AllocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_SECONDARY);
	}

	void ImGuiRenderer::Shutdown()
	{
		if (!s_ImGuiData)
			return;

		auto device = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		VK_CHECK_RESULT(vkDeviceWaitIdle(device));
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		if (s_ImGuiData->DescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, s_ImGuiData->DescriptorPool, nullptr);
			s_ImGuiData->DescriptorPool = VK_NULL_HANDLE;
		}

		delete s_ImGuiData;
		s_ImGuiData = nullptr;
	}

	void ImGuiRenderer::Render()
	{
		RenderQueueManager::Submit([]() mutable
		{
			BeginFrame();

			auto& application = Application::Get();
			application.OnImGuiRender();

			EndFrame();
		});
	}

	void ImGuiRenderer::BeginFrame()
	{
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void ImGuiRenderer::EndFrame()
	{
		CORE_ASSERT(s_ImGuiData, "ImGuiRenderer is not initialized");

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

		VK_CHECK_RESULT(vkBeginCommandBuffer(s_ImGuiData->SecondaryCommandBuffers[commandBufferIndex], &cmdBufInfo));

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = (float)height;
		viewport.height = -(float)height;
		viewport.width = (float)width;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(s_ImGuiData->SecondaryCommandBuffers[commandBufferIndex], 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.extent.width = width;
		scissor.extent.height = height;
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		vkCmdSetScissor(s_ImGuiData->SecondaryCommandBuffers[commandBufferIndex], 0, 1, &scissor);

		ImDrawData* main_draw_data = ImGui::GetDrawData();
		ImGui_ImplVulkan_RenderDrawData(main_draw_data, s_ImGuiData->SecondaryCommandBuffers[commandBufferIndex]);

		VK_CHECK_RESULT(vkEndCommandBuffer(s_ImGuiData->SecondaryCommandBuffers[commandBufferIndex]));

		std::vector<VkCommandBuffer> commandBuffers;
		commandBuffers.push_back(s_ImGuiData->SecondaryCommandBuffers[commandBufferIndex]);

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
	}

	void ImGuiRenderer::SetColorTheme()
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
}
