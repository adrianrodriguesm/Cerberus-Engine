#pragma once
namespace cerberus
{
    class ValidationLayer
    {
    public:
        // List of validation layers to use
        static std::vector<const char*> s_ValidationLayers;
#ifdef _DEBUG
        static const bool s_EnableValidationLayers = true;
        static const bool s_FullInfo = false;
#else
        static const bool s_EnableValidationLayers = true;
#endif

    public:
        ValidationLayer() = default;
        ~ValidationLayer() = default;

        void Init();
        void Shutdown();

        // Check if all of the requested layers are available
        static bool CheckValidationLayerSupport();

        static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);
        
        static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
    private:
        VkDebugUtilsMessengerEXT m_Callback;
    };
}