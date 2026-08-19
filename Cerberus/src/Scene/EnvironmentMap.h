#pragma once
#include "Renderer/Texture.h"
#include "Asset/Asset.h"
#include "Core/Handle.h"
namespace cerberus
{
	struct EnvironmentMap : public Asset
	{
		// EnvironmentMap is an asset, while the textures below are renderer-owned
		// GPU resources. Keep handles here so the asset never exposes backend object
		// ownership through shared pointers.

		::cerberus::Handle<Texture2D> EquirectangularTex;
		::cerberus::Handle<TextureCube> RadianceMap;
		::cerberus::Handle<TextureCube> IrradianceMap;
		std::string DebugName;
		bool OwnsGPUResources = true;

		EnvironmentMap() = default;
		EnvironmentMap(
			::cerberus::Handle<Texture2D> equirectangularTex,
			::cerberus::Handle<TextureCube> radianceMap,
			::cerberus::Handle<TextureCube> irradianceMap,
			bool ownsGPUResources = true)
			: EquirectangularTex(equirectangularTex),
			  RadianceMap(radianceMap),
			  IrradianceMap(irradianceMap),
			  OwnsGPUResources(ownsGPUResources)
		{
		}

		~EnvironmentMap();

		static AssetType GetStaticType() { return AssetType::EnvMap; }
		AssetType GetAssetType() const { return GetStaticType(); }

	};
}
