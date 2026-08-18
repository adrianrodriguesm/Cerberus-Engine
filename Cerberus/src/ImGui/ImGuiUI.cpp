#include "pch.h"
#include "ImGuiUI.h"
#include "backends/imgui_impl_vulkan_textures.h"
#include "Renderer/ResourceManager.h"
namespace cerberus::ui
{
	void ImageButton(Handle<Texture2D> textureHandle, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& background_col)
	{
		const ImTextureID textureID = GetTextureID(textureHandle);
		if (!textureID)
			return;

		ImGui::ImageButton(textureID, size, uv0, uv1, 0, background_col, tint_col);
	}
	void Image(Handle<Texture2D> textureHandle, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		const ImTextureID textureID = GetTextureID(textureHandle);
		if (!textureID)
			return;

		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);

	}
	void ImageButton(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& background_col)
	{
		const ImTextureID textureID = GetTextureID(texture);
		if (!textureID)
			return;

		ImGui::ImageButton(textureID, size, uv0, uv1, 0, background_col, tint_col);
	}
	void Image(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		const ImTextureID textureID = GetTextureID(texture);
		if (!textureID)
			return;

		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);
	}
	void Image(const Handle<ImageView> image, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		const ImTextureID textureID = GetTextureID(image);
		if (!textureID)
			return;

		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);
	}
	/** /
	void Image(const Handle<ImageView> image, uint32_t layer, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		auto imageInfo = image->GetImageInfo();
		imageInfo.ImageView = image->GetLayerImageView(layer);
		if (!imageInfo.ImageView)
			return;
		const auto textureID = ImGui_ImplVulkan_AddTexture(imageInfo.Sampler, imageInfo.ImageView, image->GetVulkanDescriptorInfo().imageLayout);
		ImGui::Image(textureID, size, uv0, uv1, tint_col, border_col);
	}
	/**/
}
