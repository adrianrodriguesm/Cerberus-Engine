#pragma once
#include "Renderer/Texture.h"
#include "Backend/TextureProxy.h"
#include "Core/Handle.h"
namespace cerberus
{
	class TextureManager
	{
	public:
		


		template<typename T>
		static Ref<T> GetTextureAsset(const Handle<TextureProxy>& textureProxyHandle)
		{

		}
	private:
		inline static std::map<AssetHandle, Handle<TextureProxy>> m_TextureRegistry;
	};
}