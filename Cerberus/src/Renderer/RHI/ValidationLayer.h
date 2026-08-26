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
        // Release now actually means "no validation" -- both branches used to
        // hardcode true, so Vulkan validation's substantial per-call overhead
        // (state tracking, descriptor/pipeline/barrier checks) was silently
        // inflating every Release timing measurement, scaling with draw-call
        // count rather than real GPU cost.
        static const bool s_EnableValidationLayers = false;
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