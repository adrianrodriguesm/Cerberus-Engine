#pragma once
#include "Renderer/Texture.h"
#include "Renderer/TextureAsset.h"
#include "Renderer/Texture.h"
#include "Core/Handle.h"
#include <span>
namespace cerberus
{
	class TextureManager
	{
	public:
		


		static Ref<TextureAsset> GetTextureAsset(const Handle<Texture2D>& textureHandle)
		{

		}
	private:
		inline static std::map<AssetHandle, Handle<Texture>> m_Texture2DRegistry;
	};
}

/** /
Material.SetAlbedo(TextureAsset);
Material.isDirty

Image img = ResourceManager::CreateImage();
ImageView view = ResourceManager::CreateImageView(Handle<Image> img, args..)

RenderTargetTexture texture;
Image attachment = ResourceManager::CreateImage();
ImageView attamentView = ResourceManager::CreateImageView(Handle<Image> img, args..)

TextureStorage storage;
Image storage = ResourceManager::CreateImage();
ImageView storageView = ResourceManager::CreateImageView(Handle<Image> img, args..)

// Renderer Side
BindGroup material;
material.Set(ImageView, 3);
material.Set(UBO, 2);
material.Update();
VkDescriptorSet descriptorSet = material.GetHandle();

Renderer::GetCommandBuffer()


RenterTarget rt;

	struct RenderTargetInfo
	{
		std::string DebugName;

		Handle<Image> ExistingImage;

		uint32_t Width, Height;

		ImageFormat Format = ImageFormat::RGBA;
		bool Sampler = true;
		uint32_t BaseMip = 0, MipCount = 1;
		uint32_t BaseLayer = 0, LayerCount = 1;
		ImageViewType ViewType = ImageViewType::Image2D;
		glm::vec4 ClearColor = { 0.f, 0.f, 0.f, 0.f };

		bool Resizable = true;
	};

class RenderTargetTexture
{
private:

}
{
/**/