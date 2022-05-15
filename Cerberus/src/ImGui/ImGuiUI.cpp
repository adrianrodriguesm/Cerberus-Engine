#include "pch.h"
#include "ImGuiUI.h"
#include "backends/imgui_impl_vulkan_textures.h"
namespace cerberus::ui
{
	void ImageButton(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& background_col)
	{
		const VkDescriptorImageInfo& imageInfo = texture->GetVulkanDescriptorInfo();
		if (!imageInfo.imageView)
			return;
		const auto textureID = ImGui_ImplVulkan_AddTexture(imageInfo.sampler, imageInfo.imageView, imageInfo.imageLayout);
		ImGui::ImageButton(textureID, size, uv0, uv1, 0, background_col, tint_col);
	}
	void Image(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		const VkDescriptorImageInfo& imageInfo = texture->GetVulkanDescriptorInfo();
		if (!imageInfo.imageView)
			return;
		const auto textureID = ImGui_ImplVulkan_AddTexture(imageInfo.sampler, imageInfo.imageView, imageInfo.imageLayout);
		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);

	}
	void Image(const Ref<Image2D>& image, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		const auto& imageInfo = image->GetImageInfo();
		if (!imageInfo.ImageView)
			return;
		const auto textureID = ImGui_ImplVulkan_AddTexture(imageInfo.Sampler, imageInfo.ImageView, image->GetVulkanDescriptorInfo().imageLayout);
		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);
	}

	void Image(const Ref<Image2D>& image, uint32_t layer, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		auto imageInfo = image->GetImageInfo();
		imageInfo.ImageView = image->GetLayerImageView(layer);
		if (!imageInfo.ImageView)
			return;
		const auto textureID = ImGui_ImplVulkan_AddTexture(imageInfo.Sampler, imageInfo.ImageView, image->GetVulkanDescriptorInfo().imageLayout);
		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);
	}

}