#pragma once
namespace cerberus
{
	struct DeviceProperties
	{
		struct Extension
		{
			Extension(const char* entryName, bool isOptional = false, void* pointerFeatureStruct = nullptr, uint32_t checkVersion = 0)
				: Name(entryName)
				, Optional(isOptional)
				, FeatureStruct(pointerFeatureStruct)
				, Version(checkVersion)
			{
			}

			std::string Name;
			bool        Optional{ false };
			void* FeatureStruct{ nullptr };
			uint32_t    Version{ 0 };
		};

		std::vector<Extension> DeviceExtensions;

		// Add a extension to be enabled at context creation time. If 'optional' is
		// false, context creation will fail if the extension is not supported by the
		// device. If the extension requires a feature struct, pass the initialized
		// struct to 'featureStruct'. If 'version' = 0: don't care, otherwise check
		// against equality (useful for provisional exits)
		//
		// IMPORTANT: The 'featureStruct' pointer will be stored and the object will
		// later be written to! Make sure the pointer is still valid when
		// Device gets created with the DeviceCapabilities object. All
		// featureStruct objects will be chained together and filled out with the
		// actual device capabilities during the creation of the device.
		void AddDeviceExtension(const char* name, bool optional = false, void* featureStruct = nullptr, uint32_t version = 0)
		{
			DeviceExtensions.emplace_back(name, optional, featureStruct, version);
		}

	};
	
	//////////////////////////////////////////////////////////////////////////////////// PhysicalDevice
	class PhysicalDevice
	{
	public:
		// Indices (locations) of Queue Families (if they exist at all)
		struct QueueFamilyIndices
		{
			int Graphics = -1;		// Location of Graphics Queue Family (draw to our images in the swapChain)
			int Compute	 = -1;
			int Transfer = -1;
		};
	public:
		PhysicalDevice();
		~PhysicalDevice();
		
		static Ref<PhysicalDevice> Select();

		const VkPhysicalDevice& GetVulkanPhysicalDevice() const { return m_PhysicalDevice; }
		const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }

		bool IsExtensionSupported(const std::string& extensionName) const;

		VkFormat GetDepthFormat() const { return m_DepthFormat; }
	private:
		QueueFamilyIndices GetQueueFamilyIndices(int queueFlags);
		VkFormat FindDepthFormat() const;
	private:
		// Device
		VkPhysicalDevice m_PhysicalDevice;
		// Queue Family
		QueueFamilyIndices m_QueueFamilyIndices;
		std::vector<VkQueueFamilyProperties> m_QueueFamilyProperties;
		std::vector<VkDeviceQueueCreateInfo> m_QueueCreateInfos;
		// Device Properties
		VkPhysicalDeviceProperties m_Properties;
		// Extensions
		std::unordered_set<std::string> m_SupportedExtensions;
		// Depth
		VkFormat m_DepthFormat;
		
		friend class Device;
	};
	//////////////////////////////////////////////////////////////////////////////////// Device
	class Device
	{
	public:
		Device(Ref<PhysicalDevice> physicalDevice, const DeviceProperties& deviceCapabilities);
		~Device();

		void Shutdown();

		VkDevice GetVulkanDevice() { return m_LogicalDevice; }

		Ref<PhysicalDevice> GetPhysicalDevice() const { return m_PhysicalDevice; }

		VkPhysicalDevice GetVulkanPhysicalDevice() const { return m_PhysicalDevice->GetVulkanPhysicalDevice(); }

		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		VkQueue GetComputeQueue() const { return m_ComputeQueue; }

		VkPhysicalDeviceFeatures GetDeviceFeatures() { return m_Features; }

		const VkPhysicalDeviceLimits GetPhysicalDeviceLimits() const { return m_PhysicalDevice->m_Properties.limits; }

	private:
		void InitRayTracing();

	private:
		VkDevice m_LogicalDevice;
		Ref<PhysicalDevice> m_PhysicalDevice;
		VkQueue m_GraphicsQueue, m_ComputeQueue;
		VkPhysicalDeviceFeatures m_Features;

		// TODO: (Sort this out) Ray Tracing features/extension
		// Function pointers for ray tracing related stuff
		PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
		PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
		PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
		PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
		PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
		PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR;
		PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
		PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
		PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
		PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;

		// Available features and properties
		// -- It will obtain the maximum recursion depth (e.g the number of nested ray tracing calls that can be performed from a single ray)
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR  rayTracingPipelineProperties{};
		// -- 
		VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};

		// Enabled features and properties
		VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddresFeatures{};
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures{};
		VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerationStructureFeatures{};
	};
}

