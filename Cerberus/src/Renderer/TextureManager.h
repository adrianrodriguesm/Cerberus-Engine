#pragma once
#include "Renderer/Texture.h"
#include "Renderer/TextureAsset.h"
#include "Backend/TextureProxy.h"
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
		inline static std::map<AssetHandle, Handle<Texture2D>> m_Texture2DRegistry;
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
/**/