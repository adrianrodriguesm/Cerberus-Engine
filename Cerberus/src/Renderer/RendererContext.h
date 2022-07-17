#pragma once

#include "Renderer/Swapchain.h"
#include "Renderer/Validationlayer.h"
#include <fstream>
namespace cerberus
{
	class RendererContext
	{
	public:
		RendererContext();
		~RendererContext();
		void Init();
		void Shutdown();
		
		const Ref<Device> GetDevice() const { return m_VulkanDevice; }

		static Ref<RendererContext> Get();
		static Ref<Device> GetCurrentDevice() { return RendererContext::Get()->GetDevice(); }
		static VkDevice GetCurrentVulkanDevice() { return RendererContext::Get()->GetDevice()->GetVulkanDevice(); }
		static VkInstance GetInstance() { return m_VulkanInstance; }

	private:
		// Vulkan Components
		// - Main
		static VkInstance m_VulkanInstance;
		Ref<ValidationLayer> m_ValidationLayer;
		Ref<Device> m_VulkanDevice;
		Ref<PhysicalDevice> m_VulkanPhyicalDevice;
	private:
		// Vulkan Functions
		// - Create Functions
		void CreateInstance();		
		// - Support Functions
		bool CheckInstanceExtensionSupport(std::vector<const char*>* checkExtensions);
		std::vector<const char*> GetRequiredExtensions();
	};
}

