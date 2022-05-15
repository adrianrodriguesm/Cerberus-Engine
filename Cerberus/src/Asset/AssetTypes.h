#pragma once

namespace cerberus
{
	enum class AssetFlag : uint16_t
	{
		None = 0,
		Missing = BIT(0),
		Invalid = BIT(1)
	};

	enum class AssetType : uint16_t
	{
		None = 0,
		Scene = 1,
		Mesh = 3,
		MeshSource = 4,
		Material = 6,
		Texture = 7,
		EnvMap = 8,
	};
	namespace utils 
	{

		inline AssetType AssetTypeFromString(const std::string& assetType)
		{
			if (assetType == "None")        return AssetType::None;
			if (assetType == "Scene")       return AssetType::Scene;
			if (assetType == "Mesh")        return AssetType::Mesh;
			if (assetType == "MeshSource")  return AssetType::MeshSource;
			if (assetType == "Material")    return AssetType::Material;
			if (assetType == "Texture")     return AssetType::Texture;
			if (assetType == "EnvMap")      return AssetType::EnvMap;

			CORE_ASSERT(false, "Unknown Asset Type");
			return AssetType::None;
		}

		inline const char* AssetTypeToString(AssetType assetType)
		{
			switch (assetType)
			{
			case AssetType::None:        return "None";
			case AssetType::Scene:       return "Scene";
			case AssetType::Mesh:        return "Mesh";
			case AssetType::MeshSource:  return "MeshSource";
			case AssetType::Material:    return "Material";
			case AssetType::Texture:     return "Texture";
			case AssetType::EnvMap:      return "EnvMap";
			}

			CORE_ASSERT(false, "Unknown Asset Type");
			return "None";
		}

	}
}

