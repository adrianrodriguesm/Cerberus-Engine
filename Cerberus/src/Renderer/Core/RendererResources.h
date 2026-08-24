#pragma once

#include "Renderer/RHI/Texture.h"
#include "Asset/Assets/TextureAsset.h"
#include "Asset/Assets/MaterialAsset.h"
#include "Renderer/Shader/Shader.h"
#include "Scene/EnvironmentMap.h"

namespace cerberus
{
	// Owns renderer-wide default resources.
	//
	// Renderer should coordinate lifetime, but the concrete default textures,
	// default material, empty environment and shader library live here so they
	// are not mixed into frame/command/queue state.
	class RendererResources
	{
	public:
		static void Init();
		static void Shutdown();

		static Handle<TextureCube> GetBlackCubeTexture();
		static Handle<Texture2D> GetBlackTexture();
		static Handle<Texture2D> GetWhiteTexture();
		static Handle<Texture2D> GetBRDFLutTexture();

		static Ref<MaterialAsset> GetDefaultPBRMaterial();
		static Ref<EnvironmentMap> GetEmptyEnvironmentMap();
		static ShaderLibrary& GetShaderLibrary();
	};
}
