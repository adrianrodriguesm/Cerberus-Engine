#pragma once
namespace cerberus
{
	////////////////////////////////////////////////////////////////////////////////////
	class PhysicalDevice;
	class Device;
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
		Device(Ref<PhysicalDevice> physicalDevice);
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
		VkDevice m_LogicalDevice;
		Ref<PhysicalDevice> m_PhysicalDevice;
		VkQueue m_GraphicsQueue, m_ComputeQueue;
		VkPhysicalDeviceFeatures m_Features;
	};
}

