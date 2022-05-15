#pragma once
#include "Renderer/Texture.h"
#include "Asset/Asset.h"
namespace cerberus
{
	struct EnvironmentMap : public Asset
	{
		Ref<Texture2D> EquirectangularTex;
		Ref<TextureCube> RadianceMap;
		Ref<TextureCube> IrradianceMap;
		std::string DebugName;

		EnvironmentMap() = default;
		EnvironmentMap(const Ref<Texture2D>& equirectangularTex, const Ref<TextureCube>& radianceMap, const Ref<TextureCube>& irradianceMap)
			: EquirectangularTex(equirectangularTex), RadianceMap(radianceMap), IrradianceMap(irradianceMap){}

		~EnvironmentMap()
		{
			NV_CORE_WARN("Evironment Map {0} released", DebugName);
		}

		static AssetType GetStaticType() { return AssetType::EnvMap; }
		AssetType GetAssetType() const { return GetStaticType(); }

	};
}