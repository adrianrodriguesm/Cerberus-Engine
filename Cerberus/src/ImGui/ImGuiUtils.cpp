#include "pch.h"
#include "ImGui/ImGuiUtils.h"
#include "backends/imgui_impl_vulkan_textures.h"
#include "Renderer/ResourceManager.h"

namespace cerberus::ui
{
	namespace
	{
		struct TextureIDCache
		{
			int ImGuiFrame = -1;
			std::unordered_map<Handle<ImageView>, ImTextureID> TextureIDs;
		};

		ImTextureID GetImageViewTextureID(Handle<ImageView> imageViewHandle)
		{
			ImageView* imageView = ResourceManager::GetImageView(imageViewHandle);
			if (!imageView)
				return nullptr;

			imageView->UpdateDescriptor();
			const VkDescriptorImageInfo& imageInfo = imageView->GetImageDescriptorInfo();
			if (!imageInfo.sampler || !imageInfo.imageView || imageInfo.imageLayout == VK_IMAGE_LAYOUT_UNDEFINED)
				return nullptr;

			static TextureIDCache cache;
			const int currentImGuiFrame = ImGui::GetFrameCount();
			if (cache.ImGuiFrame != currentImGuiFrame)
			{
				cache.ImGuiFrame = currentImGuiFrame;
				cache.TextureIDs.clear();
			}

			auto cachedTexture = cache.TextureIDs.find(imageViewHandle);
			if (cachedTexture != cache.TextureIDs.end())
				return cachedTexture->second;

			const ImTextureID textureID = ImGui_ImplVulkan_AddTexture(
				imageInfo.sampler,
				imageInfo.imageView,
				imageInfo.imageLayout);
			if (textureID)
				cache.TextureIDs.emplace(imageViewHandle, textureID);

			return textureID;
		}
	}

	ImTextureID GetTextureID(Handle<ImageView> imageView)
	{
		return GetImageViewTextureID(imageView);
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
