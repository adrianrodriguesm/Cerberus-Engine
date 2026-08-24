#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include <glm/glm.hpp>

#include "Core/Base.h"
#include "Asset/Asset.h"

namespace cerberus
{
	struct GPUMaterialData;

	static constexpr uint32_t InvalidMaterialTextureIndex = 0xFFFFFFFFu;

	struct MaterialTextureAssetSlots
	{
		AssetHandle AlbedoMap = 0;
		AssetHandle NormalMap = 0;
		AssetHandle MetalnessMap = 0;
		AssetHandle RoughnessMap = 0;

		bool HasAlbedoMap() const { return AlbedoMap != 0; }
		bool HasNormalMap() const { return NormalMap != 0; }
		bool HasMetalnessMap() const { return MetalnessMap != 0; }
		bool HasRoughnessMap() const { return RoughnessMap != 0; }
	};

	// CPU/high-level material data. This is renderer-agnostic and serializable.
	// RenderScene translates it into MaterialProxy / GPUMaterialData.
	struct MaterialData
	{
		glm::vec3 AlbedoColor = glm::vec3(0.8f);
		float Metalness = 0.0f;
		float Roughness = 0.4f;
		float Emission = 0.0f;
		bool UseNormalMap = false;

		MaterialTextureAssetSlots Textures;
	};

	// High-level material asset. Stores CPU-side scalar data and texture asset handles.
	// It intentionally has no Shader/BindGroup state: RenderScene translates it to
	// GPUMaterialData and each concrete SceneRenderer owns its shader implementation.
	class MaterialAsset : public Asset
	{
	public:
		explicit MaterialAsset(std::string name = "Material");
		~MaterialAsset();

		const std::string& GetName() const { return m_Name; }
		void SetName(std::string name) { m_Name = std::move(name); }

		const MaterialData& GetMaterialData() const { return m_Data; }
		GPUMaterialData BuildGPUMaterialData(
			uint32_t albedoTextureIndex = InvalidMaterialTextureIndex,
			uint32_t normalTextureIndex = InvalidMaterialTextureIndex,
			uint32_t metalnessTextureIndex = InvalidMaterialTextureIndex,
			uint32_t roughnessTextureIndex = InvalidMaterialTextureIndex) const;

		glm::vec3& GetAlbedoColor();
		void SetAlbedoColor(const glm::vec3& color);

		float& GetMetalness();
		void SetMetalness(float value);

		float& GetRoughness();
		void SetRoughness(float value);

		float& GetEmission();
		void SetEmission(float value);

		// Texture asset handles. The renderer later resolves these to GPU texture handles
		// and bindless texture indices through MaterialProxy/RenderScene.
		AssetHandle GetAlbedoMapHandle() const;
		void SetAlbedoMap(AssetHandle textureAsset);
		bool IsUsingAlbedoMap() const;
		void ClearAlbedoMap();

		AssetHandle GetNormalMapHandle() const;
		void SetNormalMap(AssetHandle textureAsset);
		bool IsUsingNormalMap() const;
		void SetUseNormalMap(bool value);
		void ClearNormalMap();

		AssetHandle GetMetalnessMapHandle() const;
		void SetMetalnessMap(AssetHandle textureAsset);
		bool IsUsingMetalnessMap() const;
		void ClearMetalnessMap();

		AssetHandle GetRoughnessMapHandle() const;
		void SetRoughnessMap(AssetHandle textureAsset);
		bool IsUsingRoughnessMap() const;
		void ClearRoughnessMap();

		static AssetType GetStaticType() { return AssetType::Material; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }

	private:
		std::string m_Name;
		MaterialData m_Data;
	};
	class MaterialTable
	{
	public:
		MaterialTable(uint32_t materialCount = 1);
		MaterialTable(Ref<MaterialTable> other);
		~MaterialTable();

		bool HasMaterial(uint32_t materialIndex) const { return m_Materials.find(materialIndex) != m_Materials.end(); }
		void SetMaterial(uint32_t index, AssetHandle material);
		void ClearMaterial(uint32_t index);

		AssetHandle GetMaterial(uint32_t materialIndex) const
		{
			CORE_ASSERT(HasMaterial(materialIndex), "Material index does not exits!");
			return m_Materials.at(materialIndex);
		}
		std::map<uint32_t, AssetHandle>& GetMaterials() { return m_Materials; }
		const std::map<uint32_t, AssetHandle>& GetMaterials() const { return m_Materials; }

		uint32_t GetMaterialCount() const { return m_MaterialCount; }
		void SetMaterialCount(uint32_t materialCount);

		void Clear();
	private:
		std::map<uint32_t, AssetHandle> m_Materials;
		uint32_t m_MaterialCount;

	};
}
