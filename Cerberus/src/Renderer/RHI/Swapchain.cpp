#include "pch.h"
#include "Swapchain.h"
#include "Core/Profiler.h"
#include "Renderer/Core/Renderer.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Renderer/Memory/DeferredResourceDeletionQueue.h"
namespace cerberus
{
	Swapchain::Swapchain(VkInstance instance, const Ref<Device>& device)
		: m_VulkanInstance(instance),m_Device(device)
	{
	}

	Swapchain::~Swapchain()
	{
	}
	////////////////////////////////////////////////////////////////////////////////////
	// Init'S
	////////////////////////////////////////////////////////////////////////////////////
	void Swapchain::InitSurface(GLFWwindow* windowHandle)
	{
		m_Window = windowHandle;
		// Create Surface(creates a surface create info struct, runs the create surface function, return result)
		VK_CHECK_RESULT(glfwCreateWindowSurface(m_VulkanInstance, windowHandle, nullptr, &m_Surface));
	}

	void Swapchain::InitSwapchain()
	{
		CreateSwapchain();
		CreateRenderPass();
		CreateFramebuffers();
		CreateCommandPool();
		AllocateCommandBuffer();
		CreateSynchronisation();
	}

	void Swapchain::UpdateNextImageToRender()
	{
		VkDevice vulkanDevice = m_Device->GetVulkanDevice();
		// -- GET NEXT IMAGE --
		// Free all the requested resources from that index
		DeferredResourceDeletionQueue::FlushFrame(m_CurrentCmdBufferIndex);

		// Get index of next image to be drawn to, and signal semaphore when ready to be drawn to
		// By setting timeout to UINT64_MAX we will always wait until the next image has been acquired or an actual error is thrown
		VkResult result;
		{
			// Blocks until a swapchain image is free -- under FIFO/vsync
			// present modes this is where "waiting for the display" actually
			// shows up, not in Present() itself.
			CORE_PROFILE_SCOPE("AcquireNextImage");
			result = vkAcquireNextImageKHR(vulkanDevice, m_Swapchain, UINT64_MAX, m_Semaphores.PresentComplete, VK_NULL_HANDLE, &m_CurrentImageIndex);
		}
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			RecreateSwapchain();
		else if (result != VK_SUCCESS)
			VK_CHECK_RESULT(result);
	}



	void Swapchain::Present()
	{
		// -- SUBMIT COMMAND BUFFER TO RENDER --
		// Queue submission information
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		// The submit info structure specifices a command buffer queue submission batch
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pWaitSemaphores = &m_Semaphores.PresentComplete;					// List of semaphores to wait on
		submitInfo.waitSemaphoreCount = 1;											// Number of semaphores to wait on
		submitInfo.pWaitDstStageMask = waitStages;									// Stages to check semaphores at
		submitInfo.pCommandBuffers = &m_CommandBuffers[m_CurrentCmdBufferIndex];	// Command buffer to submit
		submitInfo.commandBufferCount = 1;											// Number of command buffers to submit
		submitInfo.pSignalSemaphores = &m_Semaphores.RenderComplete;				// Semaphores to signal when command buffer finishes
		submitInfo.signalSemaphoreCount = 1;										// Number of semaphores to signal
		
		// Manually reset (close) fences
		VK_CHECK_RESULT(vkResetFences(m_Device->GetVulkanDevice(), 1, &m_WaitFences[m_CurrentCmdBufferIndex]));
		// Submit command buffer to queue
		VK_CHECK_RESULT(vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &submitInfo, m_WaitFences[m_CurrentCmdBufferIndex]));

		// -- PRESENT RENDERED IMAGE TO SCREEN --
		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pNext = NULL;
		presentInfo.waitSemaphoreCount = 1;										// Number of semaphores to wait on
		// Pass the semaphore signaled by the command buffer submission from the submit info as the wait semaphore for swap chain presentation
		// This ensures that the image is not presented to the windowing system until all commands have been submitted
		presentInfo.pWaitSemaphores = &m_Semaphores.RenderComplete;				// Semaphores to wait on
		presentInfo.swapchainCount = 1;											// Number of swapchains to present to
		presentInfo.pSwapchains = &m_Swapchain;									// Swapchains to present images to
		presentInfo.pImageIndices = &m_CurrentImageIndex;						// Index of images in swapchains to present

		// Present image
		VkResult result = vkQueuePresentKHR(m_Device->GetGraphicsQueue(), &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			RecreateSwapchain();
		else if (result != VK_SUCCESS)
			VK_CHECK_RESULT(result);
		
		// Update current buffer index
		const auto& config = Renderer::GetRendererConfig();
		m_CurrentCmdBufferIndex = (m_CurrentCmdBufferIndex + 1) % config.FramesInFlight;
		// Make sure the frame we're requesting has finished rendering
		VK_CHECK_RESULT(vkWaitForFences(m_Device->GetVulkanDevice(), 1, &m_WaitFences[m_CurrentCmdBufferIndex], VK_TRUE, UINT64_MAX));
		
	}

	////////////////////////////////////////////////////////////////////////////////////
	// Shutdown
	////////////////////////////////////////////////////////////////////////////////////
	void Swapchain::Shutdown()
	{
		auto vulkanDevice = m_Device->GetVulkanDevice();

		for (auto& fence : m_WaitFences)
			vkDestroyFence(vulkanDevice, fence, nullptr);

		m_WaitFences.clear();
		vkDestroySemaphore(vulkanDevice, m_Semaphores.PresentComplete, nullptr);
		vkDestroySemaphore(vulkanDevice, m_Semaphores.RenderComplete,  nullptr);

		vkDestroyCommandPool(vulkanDevice, m_CommandPool, nullptr);

		for (auto& framebuffer : m_SwapchainFrambuffers)
			vkDestroyFramebuffer(vulkanDevice, framebuffer, nullptr);

		m_SwapchainFrambuffers.clear();
		vkDestroyRenderPass(vulkanDevice, m_MainRenderPass, nullptr);

		
		for (auto& image : m_SwapchainImages)
			vkDestroyImageView(vulkanDevice, image.ImageView, nullptr);

		m_SwapchainImages.clear();
		vkDestroySwapchainKHR(vulkanDevice, m_Swapchain, nullptr);
		vkDestroySurfaceKHR(m_VulkanInstance, m_Surface, nullptr);
		m_Swapchain = VK_NULL_HANDLE;
		m_Surface = VK_NULL_HANDLE;
	}

	void Swapchain::RecreateSwapchain()
	{
		if (m_Swapchain == VK_NULL_HANDLE)
			return;

		NV_CORE_WARN("VulkanContext::OnResize");
		auto device = m_Device->GetVulkanDevice();
		vkDeviceWaitIdle(device);
		CreateSwapchain();
		CreateFramebuffers();
		vkDeviceWaitIdle(device);
	}

	////////////////////////////////////////////////////////////////////////////////////
	Swapchain::SwapchainDetails Swapchain::GetSwapchainDetails(VkPhysicalDevice physicalDevice)
	{
		SwapchainDetails swapChainDetails;
		// -- CAPABILITIES --
		// Get the surface capabilities for the given surface on the given phsical device
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_Surface, &swapChainDetails.SurfaceCapabilities);

		// -- FORMATS --
		// Get the surface capabilities for the given surface on the given phsical device
		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, &formatCount, nullptr);

		// Check if has formats and populate the list
		if (formatCount != 0)
		{
			swapChainDetails.Formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, &formatCount, swapChainDetails.Formats.data());
		}
		// -- PRESENTATION MODES --
		uint32_t presentationCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentationCount, nullptr);

		// If presetation modes returned, get list of presentaion modes
		if (presentationCount != 0)
		{
			swapChainDetails.PresentationModes.resize(presentationCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentationCount, swapChainDetails.PresentationModes.data());
		}

		return swapChainDetails;
	}

	// TODO CHAGE FORMATS TO SUPPORT AN STRUCT
	// Best format is subjective, but for ours will be:
	// Format		: VK_FORMAT_R8G8B8A8_UNORM ( VK_FORMAT_B8G8R8A8_UNORM as backup)
	// colorSpace	: VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
	VkSurfaceFormatKHR Swapchain::ChooseBestSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats)
	{
		// If only 1 format availble and is undefined then this meas ALL formatas are available (no restrictions)
		if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
		{
			return { VK_FORMAT_R8G8B8A8_UNORM,  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		}
		// If restricted search for the optimal format
		for (const auto& formats : formats)
		{
			if ((formats.format == VK_FORMAT_R8G8B8A8_UNORM || formats.format == VK_FORMAT_B8G8R8A8_UNORM) &&
				formats.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				return formats;
			}
		}
		NV_CORE_WARN("Optimal formats defined not supported");
		// If can not find optimal format, then just return first format
		return formats[0];
	}

	// We choose formats that do not risk tearing
	VkPresentModeKHR Swapchain::ChooseBestPresentationMode(std::span<const VkPresentModeKHR> presentationModes)
	{
		// Look for Mailbox presentation mode
		for (const auto& presentationMode : presentationModes)
		{
			if (presentationMode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				return presentationMode;
			}
		}
		// If can not find, use FIFO as Vulkan spec says it must be present
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D Swapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities)
	{
		// If current extent is at numeric limits, then can vary. Othewise, it is the size of the window.
		if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) //This can be done with the width or the height
		{
			// Normally the current extend is the size of the window since both 
			// the surface and the window have the same width and height
			return surfaceCapabilities.currentExtent;
		}
		// If can vary, need to set manually

		// TODO CHECK IF THE WINDOW IS NECESSARY
		//Get window size
		int width, height;
		glfwGetFramebufferSize(m_Window, &width, &height);

		// Creates a new extend using the window size
		VkExtent2D newExtent = {};
		newExtent.width = static_cast<uint32_t>(width);
		newExtent.height = static_cast<uint32_t>(height);

		// Surface also defines max and min, so make sure whitin boundaries by clamping value
		newExtent.width = std::max(surfaceCapabilities.minImageExtent.width,
			std::min(surfaceCapabilities.maxImageExtent.width, newExtent.width));
		newExtent.height = std::max(surfaceCapabilities.minImageExtent.height,
			std::min(surfaceCapabilities.maxImageExtent.height, newExtent.height));
		return newExtent;
	}

	VkImageView Swapchain::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
	{
		VkImageViewCreateInfo viewCreateInfo = {}; // Basically define the parts of the image that will be able to view
		viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.image = image;										// Image to create view for
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;					// Type of image (1D, 2D, 3D, Cube, etc)
		viewCreateInfo.format = format;										// Format of image data
		viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;		// Allows remapping of rgba components to other rgba values (e.g put in red the value of green)
		viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		// Subresources allow the view to view only a part of an image
		viewCreateInfo.subresourceRange.aspectMask = aspectFlags;			// Which aspect of image to view (e.g. COLOR_BIT for viewing colour)
		viewCreateInfo.subresourceRange.baseMipLevel = 0;					// Start mipmap level to view from
		viewCreateInfo.subresourceRange.levelCount = 1;						// Number of mipmap levels to view from the start
		viewCreateInfo.subresourceRange.baseArrayLayer = 0;					// Start array level to view from
		viewCreateInfo.subresourceRange.layerCount = 1;						// Number of array levels to view 


		// Create image view and return it
		VkImageView imageView;
		VK_CHECK_RESULT(vkCreateImageView(m_Device->GetVulkanDevice(), &viewCreateInfo, nullptr, &imageView));
		return imageView;
	}
	////////////////////////////////////////////////////////////////////////////////////
	// Main creates function
	////////////////////////////////////////////////////////////////////////////////////
	void Swapchain::CreateSwapchain()
	{
		// GET DEVICES
		VkPhysicalDevice vulkanPhysicalDevice = m_Device->GetVulkanPhysicalDevice();
		VkDevice vulkanDevice = m_Device->GetVulkanDevice();

		VkSwapchainKHR oldSwapchain = m_Swapchain;

		// --
		// Check if our queue family supports presentation TODO: IMPROVED THIS CHECK
		PhysicalDevice::QueueFamilyIndices indices = m_Device->GetPhysicalDevice()->GetQueueFamilyIndices();
		VkBool32 presentationSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(vulkanPhysicalDevice, indices.Graphics, m_Surface, &presentationSupport);
		CORE_ASSERT(presentationSupport, "Prensation Queue is not supported by the Physical Device!")
		

		// Get Swap Chain details so we can pick best settings
		SwapchainDetails swapChainDetails = GetSwapchainDetails(vulkanPhysicalDevice);

		// -- Find optimal surface values for our swapchain
		// 1. CHOOSE BEST SURFCE FORMAT
		VkSurfaceFormatKHR surfaceFormat = ChooseBestSurfaceFormat(swapChainDetails.Formats);
		// 2. CHOOSE BEST PRESENTATION MODE
		VkPresentModeKHR presentationMode = ChooseBestPresentationMode(swapChainDetails.PresentationModes);
		// 3. CHOOSE SWAP CHAIN IMAGE RESOLUTION
		VkExtent2D extent = ChooseSwapExtent(swapChainDetails.SurfaceCapabilities);

		// How many images are in the swap chain? Get 1 more than the minimum to allow triple buffer
		// --- NOTE: the minImageCount could be EQUAL to the maxImageCount and also both could be equal to ZERO (Zero means that there is not limit in the image)
		uint32_t imageCount = swapChainDetails.SurfaceCapabilities.minImageCount + 1; // + 1 to Ensure that we have triple buffer		

		// If imageCount highr than max, then clamp down to max
		// If 0, then limitless
		if (swapChainDetails.SurfaceCapabilities.maxImageCount > 0 && swapChainDetails.SurfaceCapabilities.maxImageCount < imageCount)
		{
			imageCount = swapChainDetails.SurfaceCapabilities.maxImageCount;
		}

		// Find a supported composite alpha format (not all devices support alpha opaque)
		VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		// Simply select the first composite alpha format available
		std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
			VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		};
		for (auto& compositeAlphaFlag : compositeAlphaFlags) {
			if (swapChainDetails.SurfaceCapabilities.supportedCompositeAlpha & compositeAlphaFlag) {
				compositeAlpha = compositeAlphaFlag;
				break;
			};
		}

		////////////////////////////////////////////////////////////////////////////////////
		// Swapchain Info
		////////////////////////////////////////////////////////////////////////////////////
		VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
		swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapChainCreateInfo.imageFormat = surfaceFormat.format;										// Swapchain format
		swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;								// Swapchain colour space
		swapChainCreateInfo.presentMode = presentationMode;											// Swapchain presentation mode
		swapChainCreateInfo.imageExtent = extent;													// Swapchain image extents
		swapChainCreateInfo.minImageCount = imageCount;												// Minimum image in swapchain
		swapChainCreateInfo.imageArrayLayers = 1;													// Number of layers for each image in chain
		swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;						// What attachment will be used as(in the swap chain just the color is needed)
		swapChainCreateInfo.preTransform = swapChainDetails.SurfaceCapabilities.currentTransform;	// Transform to perform on swap chain
		swapChainCreateInfo.compositeAlpha = compositeAlpha;										// How to handle blending image with external graphics (e.g. other windows) (we desided to not do any blending)
		swapChainCreateInfo.clipped = VK_TRUE;														// Whether to clip parts of image not in view (e.g. behind another window, off screen, etc)
		swapChainCreateInfo.surface = m_Surface;													// Set surface for the swapchain
		// Enable transfer source on swap chain images if supported
		if (swapChainDetails.SurfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
			swapChainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		// Enable transfer destination on swap chain images if supported
		if (swapChainDetails.SurfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
			swapChainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;		// Image not share handling
		swapChainCreateInfo.queueFamilyIndexCount = 0;							// Number of queues to share images between (e.g PrensentationQueue and GraphicQueue)
		swapChainCreateInfo.pQueueFamilyIndices = nullptr;						// Array of queue to share between
		// -- NOTE: When the windows is detroy the swapchain will be also detroy and therefore a new one will be created	
		swapChainCreateInfo.oldSwapchain = oldSwapchain; // IF old swap chain been destroyed and this one replaces it, then link old one to quickly hand over responsibilities

		// If an existing swap chain is re-created, destroy the old swap chain
		// This also cleans up all the presentable images
		if (oldSwapchain != VK_NULL_HANDLE)
		{

			for (auto& framebuffer : m_SwapchainFrambuffers)
				vkDestroyFramebuffer(vulkanDevice, framebuffer, nullptr);

			m_SwapchainFrambuffers.clear();
			for (auto& image : m_SwapchainImages)
				vkDestroyImageView(vulkanDevice, image.ImageView, nullptr);

			m_SwapchainImages.clear();
			// Crate SwapChain
			VK_CHECK_RESULT(vkCreateSwapchainKHR(vulkanDevice, &swapChainCreateInfo, nullptr, &m_Swapchain));

			vkDestroySwapchainKHR(vulkanDevice, oldSwapchain, nullptr);
		}
		else
		{
			// Crate SwapChain
			VK_CHECK_RESULT(vkCreateSwapchainKHR(vulkanDevice, &swapChainCreateInfo, nullptr, &m_Swapchain));
		}
		

		// STORE for later reference
		m_SwapchainImageFormat = surfaceFormat.format;
		m_SwapchainExtent = extent;

		uint32_t swapChainImageCount;
		vkGetSwapchainImagesKHR(vulkanDevice, m_Swapchain, &swapChainImageCount, nullptr);
		std::vector<VkImage> images(swapChainImageCount);
		vkGetSwapchainImagesKHR(vulkanDevice, m_Swapchain, &swapChainImageCount, images.data());


		for (const auto& image : images)
		{
			// Store image handle
			SwapchainImage swapchainImage;
			swapchainImage.Image = image;
			swapchainImage.ImageView = CreateImageView(image, m_SwapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
			// Add to swapchain image list
			m_SwapchainImages.push_back(swapchainImage);
		}
	}

	void Swapchain::CreateRenderPass()
	{
		// ATTACHMENTS
		// -- Color attachment of render pass
		VkAttachmentDescription colorAttachment = {};
		colorAttachment.format = m_SwapchainImageFormat;						// Format to use for attachment
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;						// Number of samples to write for multisampling
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;					// Describes what to do with attachment before rendering (in this case this op is equivalent to glClearColor)
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;					// Describes what to do with attachment after rendering
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;		// Describes what to do with stencil before rendering
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;		// Describes what to do with stencil after rendering
		// Framebuffer data will be stored as an image, but images can be given different data layouts
		// to give optimal use for certain operations
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;			// Image data layout before render pass starts
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;		// Image data layout after render pass (to change to)
		// REFERENCES
		// Attachment reference uses an attachment index that refers to index in the attachment list passed to renderPassCreateInfo
		VkAttachmentReference colorAttachmentReference = {};
		colorAttachmentReference.attachment = 0;
		colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		// Information about a particular subpass the Render Pass is using
		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;		// Pipeline type subpass is to be bound to
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentReference;
		subpass.pDepthStencilAttachment = nullptr;
		// Need to determine when layout transitions occur using subpass dependencies
		std::array<VkSubpassDependency, 2> subpassDependencies;
		// Conversion from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		// Transition must happen after...
		subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;							// Subpass index (VK_SUBPASS_EXTERNAL = Special value meaning outside of renderpass)
		subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;			// Pipeline stage (which pipeline stage need to happen before the transition takes places)
		subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;					// Stage access mask (memory access)
		// But must happen before...
		subpassDependencies[0].dstSubpass = 0;	// 0 is this the index of the fisrt sub pass (bassically the transition need to occur between the external and the 0 sub-pass)
		subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // The transition need to happen before this pipeline stage
		subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		subpassDependencies[0].dependencyFlags = 0;

		// Conversion from VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		// Transition must happen after...
		subpassDependencies[1].srcSubpass = 0;
		subpassDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;;
		// But must happen before...
		subpassDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		subpassDependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		subpassDependencies[1].dependencyFlags = 0;

		std::array< VkAttachmentDescription, 1> renderPassAttachments = { colorAttachment };

		// Create info for Render Pass
		VkRenderPassCreateInfo renderPassCreateInfo = {};
		renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCreateInfo.attachmentCount = (uint32_t)renderPassAttachments.size();
		renderPassCreateInfo.pAttachments = renderPassAttachments.data();
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpass;
		renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(subpassDependencies.size());
		renderPassCreateInfo.pDependencies = subpassDependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(m_Device->GetVulkanDevice(), &renderPassCreateInfo, nullptr, &m_MainRenderPass));

	}

	void Swapchain::CreateFramebuffers()
	{		
		VkDevice logicalDevice = m_Device->GetVulkanDevice();
		// Resize framebuffer count to equal swap chain image count
		m_SwapchainFrambuffers.resize(m_SwapchainImages.size());

		// Create a framebuffer for each swap chain image
		for (size_t i = 0; i < m_SwapchainFrambuffers.size(); i++)
		{
			std::array<VkImageView, 1> attachments = {
				m_SwapchainImages[i].ImageView
			};

			VkFramebufferCreateInfo framebufferCreateInfo = {};
			framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferCreateInfo.renderPass = m_MainRenderPass;								// Render Pass layout the Framebuffer will be used with
			framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferCreateInfo.pAttachments = attachments.data();							// List of attachments (1:1 with Render Pass)
			framebufferCreateInfo.width = m_SwapchainExtent.width;								// Framebuffer width
			framebufferCreateInfo.height = m_SwapchainExtent.height;							// Framebuffer height
			framebufferCreateInfo.layers = 1;													// Framebuffer layers

			VK_CHECK_RESULT(vkCreateFramebuffer(logicalDevice, &framebufferCreateInfo, nullptr, &m_SwapchainFrambuffers[i]));
		}
	}

	void Swapchain::CreateCommandPool()
	{
		auto& queueFamilyIndices = m_Device->GetPhysicalDevice()->GetQueueFamilyIndices();

		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndices.Graphics;	// Queue Family type that buffers from this command pool will use
		// VK_COMMAND_POOL_CREATE_TRANSIENT_BIT the command buffers allocated from the pool will be short-lived, meaning that they will be reset or freed in a relatively short timeframe
		// VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT allows any command buffer allocated from a pool to be individually reset to the initial state when calling vkBeginCommandBuffer
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		// Create a Graphics Queue Family Command Pool
		VK_CHECK_RESULT(vkCreateCommandPool(m_Device->GetVulkanDevice(), &poolInfo, nullptr, &m_CommandPool));
	}

	void Swapchain::AllocateCommandBuffer()
	{
		// Resize command buffer count to have one for each framebuffer
		m_CommandBuffers.resize(m_SwapchainFrambuffers.size());
		VkCommandBufferAllocateInfo cbAllocInfo = {};
		cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cbAllocInfo.commandPool = m_CommandPool;
		// VK_COMMAND_BUFFER_LEVEL_PRIMARY	: Buffer you submit directly to queue. Cant be called by other buffers.
		// VK_COMMAND_BUFFER_LEVEL_SECONARY	: Buffer can't be called directly. Can be called from other buffers via  
		//									  "vkCmdExecuteCommands" when recording commands in primary buffer
		cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;										
		cbAllocInfo.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());

		// Allocate command buffers and place handles in array of buffers
		VK_CHECK_RESULT(vkAllocateCommandBuffers(m_Device->GetVulkanDevice(), &cbAllocInfo, m_CommandBuffers.data()));
	}

	void Swapchain::CreateSynchronisation()
	{
		// Semaphores will stay the same during application lifetime
		// Semaphore create info
		VkSemaphoreCreateInfo semaphoreCreateInfo = {};
		semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		auto vulkanDevice = m_Device->GetVulkanDevice();
		// Create a semaphore used to synchronize image presentation
		// Ensures that the image is displayed before we start submitting new commands to the queue
		VK_CHECK_RESULT(vkCreateSemaphore(vulkanDevice, &semaphoreCreateInfo, nullptr, &m_Semaphores.PresentComplete));
		// Create a semaphore used to synchronize command submission
		// Ensures that the image is not presented until all commands have been sumbitted and executed
		VK_CHECK_RESULT(vkCreateSemaphore(vulkanDevice, &semaphoreCreateInfo, nullptr, &m_Semaphores.RenderComplete));
		
		// Wait fences to sync command buffer access
		VkFenceCreateInfo fenceCreateInfo{};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		m_WaitFences.resize(Renderer::GetRendererConfig().FramesInFlight);
		for (auto& fence : m_WaitFences)
			VK_CHECK_RESULT(vkCreateFence(m_Device->GetVulkanDevice(), &fenceCreateInfo, nullptr, &fence));
	}
	
}
