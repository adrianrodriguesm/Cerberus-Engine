#include "pch.h"
#include "ImGui/ImGuiUtils.h"
#include "backends/imgui_impl_vulkan_textures.h"
#include "Renderer/ResourceManager.h"

namespace cerberus::ui
{
	namespace
	{
		ImTextureID GetImageViewTextureID(Handle<ImageView> imageViewHandle)
		{
			ImageView* imageView = ResourceManager::GetImageView(imageViewHandle);
			if (!imageView)
				return nullptr;

			imageView->UpdateDescriptor();
			const VkDescriptorImageInfo& imageInfo = imageView->GetImageDescriptorInfo();
			if (!imageInfo.imageView)
				return nullptr;

			return ImGui_ImplVulkan_AddTexture(imageInfo.sampler, imageInfo.imageView, imageInfo.imageLayout);
		}
	}

	ImTextureID GetTextureID(Handle<Texture2D> textureHandle)
	{
		Texture2D* texture = ResourceManager::GetTexture2D(textureHandle);
		if (!texture)
			return nullptr;

		return GetImageViewTextureID(texture->GetImageView());
	}

	ImTextureID GetTextureID(const Ref<Texture2D>& texture)
	{
		if (!texture)
			return nullptr;

		return GetImageViewTextureID(texture->GetImageView());
	}
}
