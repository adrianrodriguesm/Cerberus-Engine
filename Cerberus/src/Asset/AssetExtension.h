#pragma once
#include <unordered_map>
#include "Asset/AssetTypes.h"
namespace cerberus
{
	inline static std::unordered_map<std::string, AssetType> s_AssetExtensionMap =
	{
		// Cerberus types
		{ ".cscene", AssetType::Scene },
		{ ".cmesh", AssetType::Mesh },
		{ ".cmaterial", AssetType::Material },
		// Meshes
		{ ".fbx", AssetType::MeshSource },
		{ ".gltf", AssetType::MeshSource },
		{ ".glb", AssetType::MeshSource },
		{ ".obj", AssetType::MeshSource },
		// Textures
		{ ".png", AssetType::Texture },
		{ ".jpg", AssetType::Texture },
		{ ".jpeg", AssetType::Texture },
		{ ".hdr", AssetType::EnvMap },
	};
}