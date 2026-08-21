#pragma once
#include "Renderer/Shader.h"
#include "Asset/Asset.h"

namespace cerberus
{

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

	// CPU/high-level material data. This is renderer-agnostic and can be serialized.
	// RenderScene will later translate it into MaterialProxy / GPUMaterial data.
	struct MaterialData
	{
		glm::vec3 AlbedoColor = glm::vec3(0.8f);
		float Metalness = 0.0f;
		float Roughness = 0.4f;
		float Emission = 0.0f;
		bool UseNormalMap = false;

		MaterialTextureAssetSlots Textures;
	};

	// Compact material representation intended for future raster/RT material tables.
	// Texture indices are bindless table indices and are provided by RenderScene/MaterialProxy.
	struct GPUMaterialData
	{
		glm::vec4 AlbedoColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
		// x = Roughness, y = Metalness, z = Emission, w = UseNormalMap
		glm::vec4 Parameters = glm::vec4(0.4f, 0.0f, 0.0f, 0.0f);
		glm::uvec4 TextureIndices = glm::uvec4(InvalidMaterialTextureIndex);
	};

	class Material
	{
	public:
		Material(const Ref<Shader>& shader, const std::string& name = "None");
		~Material();

		Ref<Shader> GetShader()  { return m_Shader; }
		const std::string& GetName() const  { return m_Name; }
		Buffer GetUniformBufferStorage() { return m_UniformBufferStorage; }
		void Set(const std::string& name, float value);
		void Set(const std::string& name, int value);
		void Set(const std::string& name, uint32_t value);
		void Set(const std::string& name, bool value);
		void Set(const std::string& name, const glm::ivec2& value);
		void Set(const std::string& name, const glm::ivec3& value);
		void Set(const std::string& name, const glm::ivec4& value);
		void Set(const std::string& name, const glm::vec2& value);
		void Set(const std::string& name, const glm::vec3& value);
		void Set(const std::string& name, const glm::vec4& value);
		void Set(const std::string& name, const glm::mat3& value);
		void Set(const std::string& name, const glm::mat4& value);

		template <typename T>
		void Set(const std::string& name, const T& value)
		{
			auto decl = FindUniformDeclaration(name);
			CORE_ASSERT(decl, "Could not find uniform!");
			if (!decl)
				return;

			auto& buffer = m_UniformBufferStorage;
			buffer.Write((byte*)&value, decl->GetSize(), decl->GetOffset());
		}

		float& GetFloat(const std::string& name);
		int32_t& GetInt(const std::string& name);
		uint32_t& GetUInt(const std::string& name);
		bool& GetBool(const std::string& name);
		glm::vec2& GetVector2(const std::string& name);
		glm::vec3& GetVector3(const std::string& name);
		glm::vec4& GetVector4(const std::string& name);
		glm::mat3& GetMatrix3(const std::string& name);
		glm::mat4& GetMatrix4(const std::string& name);

		template<typename T>
		T& Get(const std::string& name)
		{
			auto decl = FindUniformDeclaration(name);
			CORE_ASSERT(decl, "Could not find uniform with name 'x'");
			auto& buffer = m_UniformBufferStorage;
			return buffer.Read<T>(decl->GetOffset());
		}

	private:
		void AllocateStorage();
		const ShaderUniform* FindUniformDeclaration(const std::string& name);
	private:
		Ref<Shader> m_Shader;
		std::string m_Name;
		// TODO: Change name
		Buffer m_UniformBufferStorage;
	};

	// High-level material asset. Stores CPU-side scalar data and texture asset handles.
	class MaterialAsset : public Asset
	{
	public:
		MaterialAsset();
		MaterialAsset(Ref<Material> material);
		~MaterialAsset();


		const std::string& GetName() const;

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

		Ref<Material> GetMaterial() const { return m_Material; }
	private:
		void ApplyDataToMaterial();
	private:
		MaterialData m_Data;
		Ref<Material> m_Material;

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
