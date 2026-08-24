#include "pch.h"
#include "ValidationLayer.h"
#include "Renderer/Core/RendererContext.h"
namespace cerberus
{
    // VK_LAYER_LUNARG_standard_validation = All standard validation layers
    std::vector<const char*> ValidationLayer::s_ValidationLayers = { "VK_LAYER_KHRONOS_validation" };

    // Callback function for validation debugging (will be called when validation information record)
    // Returns a boolean that indicates if the Vulkan call that triggered the validation layer message should be aborted.
    // VK_TRUE to abort, VK_FALSE ignore
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,     // Severity of the message (e.g VERBOSE, INFO, WARNING, ERROR)       
        VkDebugUtilsMessageTypeFlagsEXT messageType,                // Type of message (e.g GENERAL, VALIDATION, PERFORMANCA)
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,  // Containing the details of the message itself
        void* pUserData)                                            // Contains a pointer that was specified during the setup of the callback and allows you to pass your own data to it
    {
        // If severity is an error The Message is important enough to abort the execution
        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            // Message is important enough to show
            NV_CORE_ERROR("Validation layer\n  Message: {0}\n", pCallbackData->pMessage);
            return VK_TRUE;
        }
        // If severity is equal than warning the Message is NOT important enough to abort the execution
        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            // Message is important enough to show
            NV_CORE_WARN("Validation layer\n Message: {0}\n", pCallbackData->pMessage);
            return VK_FALSE;
        }
#if _DEBUG
        else if(ValidationLayer::s_FullInfo)
        {
            NV_CORE_INFO("Validation layer\n Message: {0}\n", pCallbackData->pMessage);
            return VK_FALSE;
        }
#endif
        return VK_FALSE;
    }


    void ValidationLayer::Init()
    {
        auto vulkanInstance = RendererContext::GetInstance();
        // Only create callback if validation enabled
        if (!s_EnableValidationLayers) return;

        // Fill a structure with details about the messengerand its callback
        VkDebugUtilsMessengerCreateInfoEXT callbackCreateInfo = {};
        callbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        callbackCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT; // Types of severities you would like your callback to be called for
        callbackCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;        // Lets you filter which types of messages your callback is notified about
        callbackCreateInfo.pfnUserCallback = debugCallback;																																		// Pointer to callback function itself
        callbackCreateInfo.pUserData = nullptr;																																					// Optional			

        // Create debug callback with custom create function
        VK_CHECK_RESULT(CreateDebugUtilsMessengerEXT(vulkanInstance, &callbackCreateInfo, nullptr, &m_Callback));
    }

    void ValidationLayer::Shutdown()
    {
        auto vulkanIntance = RendererContext::GetInstance();
        if (s_EnableValidationLayers)
            DestroyDebugUtilsMessengerEXT(vulkanIntance, m_Callback, nullptr);
        
    }

    bool ValidationLayer::CheckValidationLayerSupport()
    {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        // Check if all the layer in validation ayers exist in availableLayers
        for (const char* layerName : s_ValidationLayers)
        {
            bool layerFound = false;

            for (const auto& layerProperties : availableLayers)
            {
                // Case of having the layer break the inner loop 
                if (strcmp(layerName, layerProperties.layerName) == 0)
                {
                    layerFound = true;
                    break;
                }
            }
            // Case of not having return false
            if (!layerFound)
            {
                return false;
            }
        }

        return true;
    }

    VkResult ValidationLayer::CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
    {
        // vkGetInstanceProcAddr returns a function pointer to the requested function in the requested instance
        // resulting function is cast as a function pointer with the header of "vkCreateDebugUtilsMessengerEXT"
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"); // Because this function is an extension function, it is not automatically loaded. We have to look up its address ourselves using vkGetInstanceProcAddr

        // If function was found, executre if with given data and return result, otherwise, return error
        if (func != nullptr)
        {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        }
        else
        {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void ValidationLayer::DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator)
    {
        // get function pointer to requested function, then cast to function pointer for vkDestroyDebugUtilsMessengerEXT
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");

        // If function found, execute
        if (func != nullptr)
        {
            func(instance, debugMessenger, pAllocator);
        }
    }

}