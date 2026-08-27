#include "pch.h"
#include "Device.h"
#include "Renderer/Core/RendererContext.h"
namespace cerberus
{
	////////////////////////////////////////////////////////////////////////////////////
	// Vulkan Physical Device
	////////////////////////////////////////////////////////////////////////////////////
	PhysicalDevice::PhysicalDevice()
	{
		VkInstance vkInstance = RendererContext::GetInstance();
		// Get the number of physical devices
		uint32_t physicalDeviceCount = 0;
		vkEnumeratePhysicalDevices(vkInstance, &physicalDeviceCount, nullptr);
		CORE_ASSERT(physicalDeviceCount > 0, "ERROR: Can't find GPUs devices that support Vulkan Instance");

		// Get the list of physical devices
		std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
		vkEnumeratePhysicalDevices(vkInstance, &physicalDeviceCount, physicalDevices.data());

		// Get the PROPERTIES and the several Physical Device
		VkPhysicalDevice selectedPhysicalDevice = nullptr;
		for (VkPhysicalDevice physicalDevice : physicalDevices)
		{
			vkGetPhysicalDeviceProperties(physicalDevice, &m_Properties);
			if (m_Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				selectedPhysicalDevice = physicalDevice;
				break;
			}
		}

		if (!selectedPhysicalDevice)
		{
			NV_CORE_TRACE("Could not find discrete GPU.");
			selectedPhysicalDevice = physicalDevices.back();
		}

		CORE_ASSERT(selectedPhysicalDevice, "Could not find any physical devices!");
		m_PhysicalDevice = selectedPhysicalDevice;

		// EXTENSIONS
		// Need to get number of extensions to create array of correct size to hold extensions
		uint32_t extensionCount = 0;
		vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, nullptr);

		// If not extension found return failure
		CORE_ASSERT(extensionCount, "Physical Device has no extension!");

		// Create a list of VkExtensionProperties using the size
		std::vector<VkExtensionProperties> extensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, extensions.data());

		if (vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, &extensions.front()) == VK_SUCCESS)
		{
			NV_CORE_TRACE("Selected physical device has {0} extensions", extensions.size());
			for (const auto& ext : extensions)
			{
				m_SupportedExtensions.emplace(ext.extensionName);
				NV_CORE_TRACE("  {0}", ext.extensionName);
			}
		}

		// QUEUE FAMILIES
		// Desired queues need to be requested upon logical device creation
		// Due to differing queue family configurations of Vulkan implementations this can be a bit tricky, especially if the application
		// requests different queue types

		// Get queue family indices for the requested queue family types
		// Note that the indices may overlap depending on the implementation
		uint32_t queueFamilyCount;
		vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, nullptr);
		CORE_ASSERT(queueFamilyCount > 0, "");
		m_QueueFamilyProperties.resize(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, m_QueueFamilyProperties.data());
		int requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
		m_QueueFamilyIndices = GetQueueFamilyIndices(requestedQueueTypes);
		CORE_ASSERT(m_QueueFamilyIndices.Graphics >= 0, "The selected GPU has no graphics queue family");
		const bool graphicsQueueSupportsCompute = m_QueueFamilyIndices.Graphics >= 0
			&& (m_QueueFamilyProperties[m_QueueFamilyIndices.Graphics].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
		CORE_ASSERT(
			graphicsQueueSupportsCompute,
			"Cerberus requires a graphics queue family that also supports compute until cross-queue ownership tracking is implemented");

		static constexpr float defaultQueuePriority = 1.0f;

		// Graphics queue
		if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT)
		{
			VkDeviceQueueCreateInfo queueInfo{};
			queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex = m_QueueFamilyIndices.Graphics;
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &defaultQueuePriority;
			m_QueueCreateInfos.push_back(queueInfo);
		}

		// Dedicated compute queue
		if (requestedQueueTypes & VK_QUEUE_COMPUTE_BIT)
		{
			if (m_QueueFamilyIndices.Compute != m_QueueFamilyIndices.Graphics)
			{
				// If compute family index differs, we need an additional queue create info for the compute queue
				VkDeviceQueueCreateInfo queueInfo{};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = m_QueueFamilyIndices.Compute;
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &defaultQueuePriority;
				m_QueueCreateInfos.push_back(queueInfo);
			}
		}

		// Dedicated transfer queue
		if (requestedQueueTypes & VK_QUEUE_TRANSFER_BIT)
		{
			if ((m_QueueFamilyIndices.Transfer != m_QueueFamilyIndices.Graphics) && (m_QueueFamilyIndices.Transfer != m_QueueFamilyIndices.Compute))
			{
				// If compute family index differs, we need an additional queue create info for the compute queue
				VkDeviceQueueCreateInfo queueInfo{};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = m_QueueFamilyIndices.Transfer;
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &defaultQueuePriority;
				m_QueueCreateInfos.push_back(queueInfo);
			}

		}
		// Find Depth Format
		m_DepthFormat = FindDepthFormat();
	}
	PhysicalDevice::~PhysicalDevice()
	{

	}

	Ref<PhysicalDevice> PhysicalDevice::Select()
	{
		return CreateRef<PhysicalDevice>();
	}

	bool PhysicalDevice::IsExtensionSupported(const std::string& extensionName) const
	{
		return m_SupportedExtensions.find(extensionName) != m_SupportedExtensions.end();
	}

	PhysicalDevice::QueueFamilyIndices PhysicalDevice::GetQueueFamilyIndices(int flags)
	{

		QueueFamilyIndices indices;

		// Immediate graphics and compute commands share one queue before the engine
		// has explicit cross-queue ownership tracking. Prefer a family that supports
		// every operation required by that path.
		if (flags & VK_QUEUE_GRAPHICS_BIT)
		{
			VkQueueFlags requiredFlags = VK_QUEUE_GRAPHICS_BIT;
			if (flags & VK_QUEUE_COMPUTE_BIT)
				requiredFlags |= VK_QUEUE_COMPUTE_BIT;

			for (uint32_t i = 0; i < m_QueueFamilyProperties.size(); i++)
			{
				if ((m_QueueFamilyProperties[i].queueFlags & requiredFlags) == requiredFlags)
				{
					indices.Graphics = i;
					break;
				}
			}
		}

		// Dedicated queue for compute
		// Try to find a queue family index that supports compute but not graphics
		if (flags & VK_QUEUE_COMPUTE_BIT)
		{
			for (uint32_t i = 0; i < m_QueueFamilyProperties.size(); i++)
			{
				auto& queueFamilyProperties = m_QueueFamilyProperties[i];
				if ((queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) && ((queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0))
				{
					indices.Compute = i;
					break;
				}
			}
		}

		// Dedicated queue for transfer
		// Try to find a queue family index that supports transfer but not graphics and compute
		if (flags & VK_QUEUE_TRANSFER_BIT)
		{
			for (uint32_t i = 0; i < m_QueueFamilyProperties.size(); i++)
			{
				auto& queueFamilyProperties = m_QueueFamilyProperties[i];
				if ((queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT) && ((queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) && ((queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) == 0))
				{
					indices.Transfer = i;
					break;
				}
			}
		}

		// For other queue types or if no separate compute queue is present, return the first one to support the requested flags
		for (uint32_t i = 0; i < m_QueueFamilyProperties.size(); i++)
		{
			if ((flags & VK_QUEUE_TRANSFER_BIT) && indices.Transfer == -1)
			{
				if (m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
					indices.Transfer = i;
			}

			if ((flags & VK_QUEUE_COMPUTE_BIT) && indices.Compute == -1)
			{
				if (m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
					indices.Compute = i;
			}

			if ((flags & VK_QUEUE_GRAPHICS_BIT) && indices.Graphics == -1)
			{
				if (m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
					indices.Graphics = i;
			}
		}

		return indices;
	}
	VkFormat PhysicalDevice::FindDepthFormat() const
	{
		// Since all depth formats may be optional, we need to find a suitable depth format to use
		// Start with the highest precision packed format
		std::vector<VkFormat> depthFormats = 
		{
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_D16_UNORM_S8_UINT,
			VK_FORMAT_D16_UNORM
		};
		for (auto& format : depthFormats)
		{
			VkFormatProperties formatProps;
			vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &formatProps);
			// Format must support depth stencil attachment for optimal tiling
			if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
				return format;
		}
		return VK_FORMAT_UNDEFINED;
	}
	////////////////////////////////////////////////////////////////////////////////////
	// Vulkan Device
	////////////////////////////////////////////////////////////////////////////////////
	Device::Device(Ref<PhysicalDevice> physicalDevice, const DeviceProperties& deviceCapabilities)
		: m_PhysicalDevice(physicalDevice)
	{
		// Do we need to enable any other extensions (e.g. NV_RAYTRACING?)
		std::vector<const char*> deviceExtensions;

		for (const auto& extension : deviceCapabilities.DeviceExtensions)
		{
			// If the device will be used for presenting to a display via a swapchain we need to request the swapchain extension
			NV_CORE_INFO("{0} extension supported by the Physical Device!", extension.Name);
			CORE_ASSERT(m_PhysicalDevice->IsExtensionSupported(extension.Name), "Swapchain extension not supported by the Physical Device!");
			deviceExtensions.push_back(extension.Name.c_str());
		}


		// Physical Device Features the logical device will be using
		VkPhysicalDeviceFeatures deviceFeatures = {};
		deviceFeatures.samplerAnisotropy = VK_TRUE; // Enable Anisotropy filtering
		deviceFeatures.pipelineStatisticsQuery = VK_TRUE;

		// Information to create logical device (sometimes called "device")
		VkDeviceCreateInfo deviceCreateInfo = {};
		deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(m_PhysicalDevice->m_QueueCreateInfos.size());	// Number of Queue Create Info
		deviceCreateInfo.pQueueCreateInfos = m_PhysicalDevice->m_QueueCreateInfos.data();							// List of queue create infos so device can create required queues
		deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());					// Number of enabled logical device extensions
		deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();											// List of enabled logical device extensions
		deviceCreateInfo.pEnabledFeatures = &deviceFeatures;														// Physical Device features Logical Device will use

		// RAY TRACING FEATURES
		// If a pNext(Chain) has been passed, we need to add it to the device creation info
		VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};

		VkPhysicalDeviceDescriptorIndexingFeatures supportedDescriptorIndexingFeatures{};
		supportedDescriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
		VkPhysicalDeviceBufferDeviceAddressFeatures supportedBufferDeviceAddressFeatures{};
		supportedBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR supportedRayTracingPipelineFeatures{};
		supportedRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
		VkPhysicalDeviceAccelerationStructureFeaturesKHR supportedAccelerationStructureFeatures{};
		supportedAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		VkPhysicalDeviceHostQueryResetFeatures supportedHostQueryResetFeatures{};
		supportedHostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;

		// The instance requests VK_API_VERSION_1_3, but that alone does not enable
		// any 1.3 feature; Dynamic Rendering must be queried and opted into explicitly.
		VkPhysicalDeviceVulkan13Features supportedVulkan13Features{};
		supportedVulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		supportedVulkan13Features.pNext = &supportedHostQueryResetFeatures;
		supportedHostQueryResetFeatures.pNext = &supportedAccelerationStructureFeatures;
		supportedAccelerationStructureFeatures.pNext = &supportedRayTracingPipelineFeatures;
		supportedRayTracingPipelineFeatures.pNext = &supportedDescriptorIndexingFeatures;
		supportedDescriptorIndexingFeatures.pNext = &supportedBufferDeviceAddressFeatures;

		VkPhysicalDeviceFeatures2 supportedDeviceFeatures{};
		supportedDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		supportedDeviceFeatures.pNext = &supportedVulkan13Features;
		vkGetPhysicalDeviceFeatures2(m_PhysicalDevice->GetVulkanPhysicalDevice(), &supportedDeviceFeatures);

		CORE_ASSERT(supportedDeviceFeatures.features.samplerAnisotropy,
			"The selected GPU does not support sampler anisotropy");
		CORE_ASSERT(supportedDeviceFeatures.features.pipelineStatisticsQuery,
			"The selected GPU does not support pipeline statistics queries");
		CORE_ASSERT(
			supportedDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing,
			"The selected GPU does not support non-uniform sampled-image array indexing required by the RT texture table");
		CORE_ASSERT(
			supportedVulkan13Features.dynamicRendering == VK_TRUE,
			"The selected GPU does not support Vulkan 1.3 Dynamic Rendering");
		CORE_ASSERT(supportedBufferDeviceAddressFeatures.bufferDeviceAddress,
			"The selected GPU does not support buffer device addresses");
		CORE_ASSERT(supportedRayTracingPipelineFeatures.rayTracingPipeline,
			"The selected GPU does not support ray tracing pipelines");
		CORE_ASSERT(supportedAccelerationStructureFeatures.accelerationStructure,
			"The selected GPU does not support acceleration structures");

		VkPhysicalDeviceDescriptorIndexingFeatures enabledDescriptorIndexingFeatures{};
		enabledDescriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
		enabledDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

		// Enable features required for ray tracing using feature chaining via pNext
		VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddresFeatures = {};
		enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
		enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;
		enabledDescriptorIndexingFeatures.pNext = &enabledBufferDeviceAddresFeatures;

		VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures{};
		enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
		enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
		enabledRayTracingPipelineFeatures.pNext = &enabledDescriptorIndexingFeatures;

		VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerationStructureFeatures{};
		enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
		enabledAccelerationStructureFeatures.pNext = &enabledRayTracingPipelineFeatures;

		// Core Vulkan 1.3 feature: no extension string required, only this feature
		// struct. Kept as the new head of the chain so every previously enabled
		// feature (acceleration structures, RT pipeline,
		// descriptor indexing, buffer device address) stays enabled unchanged.
		VkPhysicalDeviceVulkan13Features enabledVulkan13Features{};
		enabledVulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		enabledVulkan13Features.dynamicRendering = VK_TRUE;
		// Query pools are reset on the GPU with vkCmdResetQueryPool, so the
		// optional hostQueryReset feature is not required.
		enabledVulkan13Features.pNext = &enabledAccelerationStructureFeatures;

		void* deviceCreatepNextChain = &enabledVulkan13Features;

		physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		physicalDeviceFeatures2.features = deviceFeatures;
		physicalDeviceFeatures2.pNext = deviceCreatepNextChain;


		deviceCreateInfo.pEnabledFeatures = nullptr;
		deviceCreateInfo.pNext = &physicalDeviceFeatures2;



		//CREATE LOGICAL DEVICE
		VK_CHECK_RESULT(vkCreateDevice(m_PhysicalDevice->GetVulkanPhysicalDevice(), &deviceCreateInfo, nullptr, &m_LogicalDevice));

		// Queues are created at the same time as the device...
		// So we want handle queues
		// From given logical device, of given Queue family, of given Queue Index (0 since only one queue), place reference in given vkQueuek
		vkGetDeviceQueue(m_LogicalDevice, m_PhysicalDevice->m_QueueFamilyIndices.Graphics, 0, &m_GraphicsQueue);
		vkGetDeviceQueue(m_LogicalDevice, m_PhysicalDevice->m_QueueFamilyIndices.Compute, 0, &m_ComputeQueue);

		QueryRayTracingProperties();
	}
	Device::~Device()
	{
	}
	void Device::Shutdown()
	{
		vkDestroyDevice(m_LogicalDevice, nullptr);
		m_LogicalDevice = VK_NULL_HANDLE;
	}

	void Device::QueryRayTracingProperties()
	{
		
		VkPhysicalDevice physicalDevice = m_PhysicalDevice->GetVulkanPhysicalDevice();
		// Get ray tracing pipeline properties, which will be used later on in the sample
		m_RayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
		VkPhysicalDeviceProperties2 deviceProperties2{};
		deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		deviceProperties2.pNext = &m_RayTracingPipelineProperties;
		vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);

		// Get acceleration structure properties, which will be used later on in the sample
		m_AccelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
		deviceProperties2.pNext = &m_AccelerationStructureProperties;
		vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);
	}


}
