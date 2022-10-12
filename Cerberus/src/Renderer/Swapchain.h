#pragma once
#include "Renderer/Device.h"
#include "Renderer/ResourceAllocator.h"
namespace cerberus
{
	class Swapchain
	{
	public:
		struct SwapchainDetails
		{
			VkSurfaceCapabilitiesKHR SurfaceCapabilities;		// Surface properties, e.g. image size/extent
			std::vector<VkSurfaceFormatKHR> Formats;			// Surface image formats, e.g RBB and size of each colour
			std::vector<VkPresentModeKHR> PresentationModes;	// How images should be presented to presented to screen (e.g timing and they deal with screen tearing)
		};
		struct SwapchainImage
		{
			VkImage Image;					// We get the image from the swapchain
			VkImageView ImageView;			// In order to interact with an image we created an ImageView
		};
	public:
		Swapchain(VkInstance instance, const Ref<Device>& device);
		~Swapchain();

		void InitSurface(GLFWwindow* windowHandle);
		void InitSwapchain();
		void UpdateNextImageToRender();
		void Present();
		void Shutdown();

		void RecreateSwapchain();

		uint32_t GetWidth() const { return m_SwapchainExtent.width; }
		uint32_t GetHeight() const { return m_SwapchainExtent.height; }

		inline uint32_t GetImageCount() const { return (uint32_t)m_SwapchainImages.size(); }
		inline VkRenderPass GetRenderPass() const { return m_MainRenderPass; }
		inline VkFormat GetSwapchainImageFormat() const { return m_SwapchainImageFormat; }
		inline VkExtent2D GetSwapchainExtent() const { return m_SwapchainExtent; }
		inline uint32_t GetCurrentBufferIndex() const { return m_CurrentCmdBufferIndex; }
		inline VkFramebuffer GetActiveFramebuffer() const { return m_SwapchainFrambuffers[m_CurrentImageIndex]; }
		inline VkCommandBuffer GetDrawCommandBuffer(uint32_t index) const
		{ 
			CORE_ASSERT(index < m_SwapchainImages.size(), "Overflow! Invalid render command buffer for the swapchain!"); 
			return m_CommandBuffers[index]; 
		}

		inline VkCommandBuffer GetCurrentDrawCommandBuffer() const
		{
			return m_CommandBuffers[m_CurrentCmdBufferIndex];
		}

		// TODO: Sort this out
		inline const std::vector<Swapchain::SwapchainImage>& GetSwapchainImages() const { return m_SwapchainImages; }

	private:
		// -- Main creates functions
		void CreateSwapchain();
		void CreateRenderPass();
		// Create the depth buffer and stencil buffer image
		void CreateDepthStencil();
		void CreateFramebuffers();
		void CreateCommandPool();
		void AllocateCommandBuffer();
		void CreateSynchronisation();
		// -- Getters Functions
		SwapchainDetails GetSwapchainDetails(VkPhysicalDevice physicalDevice);

		// -- Choose Functions
		VkSurfaceFormatKHR ChooseBestSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
		VkPresentModeKHR ChooseBestPresentationMode(const std::vector<VkPresentModeKHR>& presentationModes);
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities);
		// -- Creates Support Functions
		VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
		
	private:
		VkInstance m_VulkanInstance;
		Ref<Device> m_Device;

		VkQueue m_PresentationQueue;
		// Surface
		VkSurfaceKHR m_Surface; // To interact with the window
		GLFWwindow* m_Window;
		// Swapchain
		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		std::vector<Swapchain::SwapchainImage> m_SwapchainImages;
		// - Utility
		VkFormat m_SwapchainImageFormat;
		VkExtent2D m_SwapchainExtent;
		// Render Pass and Framebuffer
		VkRenderPass m_MainRenderPass;
		std::vector<VkFramebuffer> m_SwapchainFrambuffers; // One for each swapchain image
		// Commad buffers and pool
		std::vector<VkFramebuffer> m_Framebuffers;
		VkCommandPool m_CommandPool = nullptr;
		std::vector<VkCommandBuffer> m_CommandBuffers;
		std::vector<VkFence> m_WaitFences;
		struct
		{
			// Swapchain
			VkSemaphore PresentComplete;
			// Command buffer
			VkSemaphore RenderComplete;
		} m_Semaphores;

		uint32_t m_CurrentCmdBufferIndex = 0;
		uint32_t m_CurrentImageIndex = 0;

		// NOTE: We don't more need more than one depth image because 
		//		 we only use it for drawing not for presenting
		// Depth Buffer Image
		/** /
		struct
		{
			VkImage Image;					// We get the image from the swapchain
			VkImageView ImageView;			// In order to interact with an image we created an ImageView
			VmaAllocation MemoryAllocation;
		} m_DepthStencil;
		/**/
	};
};

