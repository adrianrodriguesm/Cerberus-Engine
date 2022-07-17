#include "pch.h"
#include "RendererContext.h"
#include "Renderer/Renderer.h"
#include "Renderer/VulkanExtensions.h"
namespace cerberus
{
	VkInstance RendererContext::m_VulkanInstance = nullptr;
	RendererContext::RendererContext()
	{

	}
	RendererContext::~RendererContext()
	{

	}
	void RendererContext::Init()
	{
		
		// Check validation layer
		if (ValidationLayer::s_EnableValidationLayers && !ValidationLayer::CheckValidationLayerSupport())
			CORE_ASSERT(false, "Required Validation Layers not supported!");
		
		NV_CORE_INFO("...");
		CreateInstance();
		NV_CORE_INFO("Vulkan Instace created with success");
		NV_CORE_INFO("...");
		m_ValidationLayer = CreateRef<ValidationLayer>();
		m_ValidationLayer->Init();
		NV_CORE_INFO("Debug callback created with success");
		NV_CORE_INFO("...");
		m_VulkanPhyicalDevice = PhysicalDevice::Select();
		NV_CORE_INFO("Vulkan Physical Device selected with success");
		NV_CORE_INFO("...");
		{
			DeviceProperties deviceCapabilities;
			// Activate the swapchain extension
			deviceCapabilities.AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			// Activate the ray tracing extension
			VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
			deviceCapabilities.AddDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &accelFeature);  // To build acceleration structures
			VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
			deviceCapabilities.AddDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, false, &rtPipelineFeature);  // To use vkCmdTraceRaysKHR
			deviceCapabilities.AddDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);  // Required by ray tracing pipeline
			m_VulkanDevice = CreateRef<Device>(m_VulkanPhyicalDevice, deviceCapabilities);
		}
		extensions::LoadVKExtensions(m_VulkanInstance, vkGetInstanceProcAddr, m_VulkanDevice->GetVulkanDevice(), vkGetDeviceProcAddr);
		NV_CORE_INFO("Vulkan Logical Device created with success");
		NV_CORE_INFO("...");

		VulkanAllocator::Init(m_VulkanDevice);
		
	}
	void RendererContext::Shutdown()
	{
		auto vulkanLogicalDevice = m_VulkanDevice->GetVulkanDevice();
		
		VulkanAllocator::Shutdown();

		m_VulkanDevice->Shutdown();
		m_ValidationLayer->Shutdown();
		vkDestroyInstance(m_VulkanInstance, nullptr);
		
		m_VulkanInstance = VK_NULL_HANDLE;

		
	}

	Ref<RendererContext> RendererContext::Get()
	{
		return Renderer::GetRenderContext(); 
	}

	void RendererContext::CreateInstance() 
	{
		////////////////////////////////////////////////////////////////////////////////////
		// Application Info
		////////////////////////////////////////////////////////////////////////////////////
		// Information about the Application itself
		// Most data here does not affect the program and is for developer convenience
		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Cerberus";						// Custom name of the application
		//appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);	// Custom version of the application
		appInfo.pEngineName = "Cerberus";							// Custom name of the engine
		//appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);			// Custon version of the engine
		appInfo.apiVersion = VK_API_VERSION_1_3;					// The Vulkan Version

		// Necessary info for a creation of a VkInstance 
		VkInstanceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		// Set up extensions that the Instance will use									
		auto intanceExtension = GetRequiredExtensions();

		// Check instance extension supported
		ASSERT(CheckInstanceExtensionSupport(&intanceExtension), "Vulkan Instance does not support required extensions!");

		createInfo.enabledExtensionCount = static_cast<uint32_t>(intanceExtension.size());
		createInfo.ppEnabledExtensionNames = intanceExtension.data();

		createInfo.enabledLayerCount = 0;
		createInfo.ppEnabledLayerNames = nullptr;

		// If validation enabled, include the validation layer names
		if (ValidationLayer::s_EnableValidationLayers)
		{
			createInfo.enabledLayerCount = static_cast<uint32_t>(ValidationLayer::s_ValidationLayers.size());
			createInfo.ppEnabledLayerNames = ValidationLayer::s_ValidationLayers.data();
		}
		else
		{
			createInfo.enabledLayerCount = 0;
			createInfo.ppEnabledLayerNames = nullptr;
		}

		// VkAllocationCallbacks(2º parameter) is a paremeter that 
		// allow us to specify where and how store the memory -> Require good knowledge in memory manegement
		VK_CHECK_RESULT(vkCreateInstance(&createInfo, nullptr, &(m_VulkanInstance)));
	}

	

	

	bool RendererContext::CheckInstanceExtensionSupport(std::vector<const char*>* checkExtensions)
	{
		// Need to get number of extensions to create array of correct size to hold extensions
		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

		// Create a list of VkExtensionProperties using the size
		std::vector<VkExtensionProperties> extensions(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

		// Check if a given extension are in a list of avalible extensions
		for (auto& checkExtension : *checkExtensions)
		{
			bool hasExtension = false;
			for (auto& extension : extensions)
			{
				if (strcmp(checkExtension, extension.extensionName) == 0)
				{
					hasExtension = true;
					break;
				}
			}

			if (!hasExtension)
				return false;
		}

		return true;
	}
	std::vector<const char*> RendererContext::GetRequiredExtensions()
	{
		uint32_t glfwExtensionCount = 0;    // GLFW may require mulple extensions
		const char** glfwExtensions;     	// Extension passed as arrray of cstrings, so need pointer (the array) to pointer to (the cstring)
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (ValidationLayer::s_EnableValidationLayers)
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

		return extensions;
	}

}