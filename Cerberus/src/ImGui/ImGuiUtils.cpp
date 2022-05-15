#include "pch.h"
#include "ImGui/ImGuiUtils.h"
#include "backends/imgui_impl_vulkan_textures.h"

namespace cerberus::ui
{
	ImTextureID GetTextureID(Ref<Texture2D> texture)
	{      
        const VkDescriptorImageInfo& imageInfo = texture->GetVulkanDescriptorInfo();
        if (!imageInfo.imageView)
            return nullptr;

        return ImGui_ImplVulkan_AddTexture(imageInfo.sampler, imageInfo.imageView, imageInfo.imageLayout);   
	}
}