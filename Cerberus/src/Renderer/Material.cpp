#include "pch.h"
#include "Material.h"
namespace cerberus
{
	MaterialAsset::MaterialAsset(std::string name)
		: m_Name(std::move(name))
	{
	}

	MaterialAsset::~MaterialAsset()
	{
	}

	GPUMaterialData MaterialAsset::BuildGPUMaterialData(
		uint32_t albedoTextureIndex,
		uint32_t normalTextureIndex,
		uint32_t metalnessTextureIndex,
		uint32_t roughnessTextureIndex) const
	{
		GPUMaterialData data{};
		data.AlbedoColor = glm::vec4(m_Data.AlbedoColor, 1.0f);
		data.Parameters = glm::vec4(
			m_Data.Roughness,
			m_Data.Metalness,
			m_Data.Emission,
			m_Data.UseNormalMap ? 1.0f : 0.0f);
		data.TextureIndices = glm::uvec4(
			albedoTextureIndex,
			normalTextureIndex,
			metalnessTextureIndex,
			roughnessTextureIndex);
		return data;
	}

	glm::vec3& MaterialAsset::GetAlbedoColor()
	{
		return m_Data.AlbedoColor;
	}

	void MaterialAsset::SetAlbedoColor(const glm::vec3& color)
	{
		m_Data.AlbedoColor = color;
	}

	float& MaterialAsset::GetMetalness()
	{
		return m_Data.Metalness;
	}

	void MaterialAsset::SetMetalness(float value)
	{
		m_Data.Metalness = value;
	}

	float& MaterialAsset::GetRoughness()
	{
		return m_Data.Roughness;
	}

	void MaterialAsset::SetRoughness(float value)
	{
		m_Data.Roughness = value;
	}

	float& MaterialAsset::GetEmission()
	{
		return m_Data.Emission;
	}

	void MaterialAsset::SetEmission(float value)
	{
		m_Data.Emission = value;
	}

	AssetHandle MaterialAsset::GetAlbedoMapHandle() const
	{
		return m_Data.Textures.AlbedoMap;
	}

	void MaterialAsset::SetAlbedoMap(AssetHandle textureAsset)
	{
		m_Data.Textures.AlbedoMap = textureAsset;
	}

	bool MaterialAsset::IsUsingAlbedoMap() const
	{
		return m_Data.Textures.HasAlbedoMap();
	}

	void MaterialAsset::ClearAlbedoMap()
	{
		m_Data.Textures.AlbedoMap = 0;
	}

	AssetHandle MaterialAsset::GetNormalMapHandle() const
	{
		return m_Data.Textures.NormalMap;
	}

	void MaterialAsset::SetNormalMap(AssetHandle textureAsset)
	{
		m_Data.Textures.NormalMap = textureAsset;
		m_Data.UseNormalMap = textureAsset != 0;
	}

	bool MaterialAsset::IsUsingNormalMap() const
	{
		return m_Data.UseNormalMap && m_Data.Textures.HasNormalMap();
	}

	void MaterialAsset::SetUseNormalMap(bool value)
	{
		m_Data.UseNormalMap = value;
	}

	void MaterialAsset::ClearNormalMap()
	{
		m_Data.Textures.NormalMap = 0;
		SetUseNormalMap(false);
	}

	AssetHandle MaterialAsset::GetMetalnessMapHandle() const
	{
		return m_Data.Textures.MetalnessMap;
	}

	void MaterialAsset::SetMetalnessMap(AssetHandle textureAsset)
	{
		m_Data.Textures.MetalnessMap = textureAsset;
	}

	bool MaterialAsset::IsUsingMetalnessMap() const
	{
		return m_Data.Textures.HasMetalnessMap();
	}

	void MaterialAsset::ClearMetalnessMap()
	{
		m_Data.Textures.MetalnessMap = 0;
	}

	AssetHandle MaterialAsset::GetRoughnessMapHandle() const
	{
		return m_Data.Textures.RoughnessMap;
	}

	void MaterialAsset::SetRoughnessMap(AssetHandle textureAsset)
	{
		m_Data.Textures.RoughnessMap = textureAsset;
	}

	bool MaterialAsset::IsUsingRoughnessMap() const
	{
		return m_Data.Textures.HasRoughnessMap();
	}

	void MaterialAsset::ClearRoughnessMap()
	{
		m_Data.Textures.RoughnessMap = 0;
	}

	///////////////////////////////////////////////////////////////
	///// MaterialTable
	///////////////////////////////////////////////////////////////
	MaterialTable::MaterialTable(uint32_t materialCount)
		: m_MaterialCount(materialCount)
	{
	}

	MaterialTable::MaterialTable(Ref<MaterialTable> other)
		: m_MaterialCount(other->m_MaterialCount)
	{
		const auto& materials = other->GetMaterials();
		for (auto [index, materialAsset] : materials)
			SetMaterial(index, materialAsset);
	}

	MaterialTable::~MaterialTable()
	{
		m_Materials.clear();
	}

	void MaterialTable::SetMaterial(uint32_t index, AssetHandle material)
	{
		m_Materials[index] = material;
		if (index >= m_MaterialCount)
			m_MaterialCount = index + 1;
	}

	void MaterialTable::ClearMaterial(uint32_t index)
	{
		CORE_ASSERT(HasMaterial(index), "Material index does not exist!");
		m_Materials.erase(index);
		if (index >= m_MaterialCount)
			m_MaterialCount = index - 1; // TODO: Check this
	}

	void MaterialTable::SetMaterialCount(uint32_t materialCount)
	{
		m_Materials.clear();
		m_MaterialCount = materialCount;
	}

	void MaterialTable::Clear()
	{
		m_Materials.clear();
	}
}
