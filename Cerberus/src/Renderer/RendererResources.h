#pragma once

#include "Renderer/Texture.h"
#include "Renderer/TextureAsset.h"
#include "Renderer/Material.h"
#include "Renderer/Shader.h"
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
