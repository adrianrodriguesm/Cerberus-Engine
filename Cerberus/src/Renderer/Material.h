#pragma once
#include "Renderer/Shader.h"
#include "Renderer/TextureAsset.h"
#include "Renderer/Texture.h"
#include "Asset/Asset.h"

namespace cerberus
{
	/**/
	class Material
	{
	public:
		Material(const Ref<Shader>& shader, const std::string& name = "None", bool persistent = true);
		~Material();

		Ref<Shader> GetShader()  { return m_Shader; }
		const std::string& GetName() const  { return m_Name; }
		Buffer GetUniformBufferStorage() { return m_UniformBufferStorage; }
		const VkDescriptorSet GetDescriptorSet(uint32_t frameIndex) const { return !m_DescriptorSets.empty() ? m_DescriptorSets[frameIndex] : nullptr; }
		// Render Thread
		void UpdateForRender(const std::vector<std::vector<std::vector<VkWriteDescriptorSet>>>& uniformBufferWriteDescriptors = std::vector<std::vector<std::vector<VkWriteDescriptorSet>>>());
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

		void Set(const std::string& name, const Ref<TextureCube>& texture);
		void Set(const std::string& name, const Ref<Texture2D>& texture);
		void Set(const std::string& name, const Ref<Image>& image);
		//void Set(const std::string& name, const Ref<Texture2D>& texture, uint32_t arrayIndex);

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

		Ref<Texture2D> GetTexture2D(const std::string& name);
		Ref<TextureCube> GetTextureCube(const std::string& name);

		Ref<Texture2D> TryGetTexture2D(const std::string& name);
		Ref<TextureCube> TryGetTextureCube(const std::string& name);

		template<typename T>
		T& Get(const std::string& name)
		{
			auto decl = FindUniformDeclaration(name);
			CORE_ASSERT(decl, "Could not find uniform with name 'x'");
			auto& buffer = m_UniformBufferStorage;
			return buffer.Read<T>(decl->GetOffset());
		}

		template<typename T>
		Ref<T> GetResource(const std::string& name)
		{
			auto decl = FindResourceDeclaration(name);
			CORE_ASSERT(decl, "Could not find uniform with name 'x'");
			uint32_t slot = decl->GetRegister();
			CORE_ASSERT(slot < m_Textures.size(), "Texture slot is invalid!");
			return std::dynamic_pointer_cast<T>(m_Textures[slot]);
		}

		template<typename T>
		Ref<T> TryGetResource(const std::string& name)
		{
			auto decl = FindResourceDeclaration(name);
			if (!decl)
				return nullptr;

			uint32_t slot = decl->GetRegister();
			if (slot >= m_Textures.size())
				return nullptr;

			return std::dynamic_pointer_cast<T>(m_Textures[slot]);
		}

	private:
		enum class PendingDescriptorType
		{
			None = 0, Texture2D, TextureCube, Image2D
		};
		struct PendingDescriptor
		{
			PendingDescriptorType Type = PendingDescriptorType::None;
			VkWriteDescriptorSet WriteDescriptorSet;
			Ref<Texture> Texture;
			Ref<Image> Image;
		};
	private:
		void AllocateStorage();
		const ShaderUniform* FindUniformDeclaration(const std::string& name);
		const ShaderResourceDeclaration* FindResourceDeclaration(const std::string& name);
	private:
		Ref<Shader> m_Shader;
		std::string m_Name;
		// TODO: Change name
		Buffer m_UniformBufferStorage;
		std::vector<Ref<Texture>> m_Textures;
		std::vector<Ref<Image>> m_Images;
		std::unordered_map<uint32_t, std::shared_ptr<PendingDescriptor>> m_ResidentDescriptors;
		std::vector<VkDescriptorSet> m_DescriptorSets;
		std::vector<std::vector<VkWriteDescriptorSet>> m_WriteDescriptors;
		bool m_Persistent = true;
	};

	class MaterialAsset : public Asset
	{
	public:
		MaterialAsset();
		MaterialAsset(Ref<Material> material);
		~MaterialAsset();


		const std::string& GetName() const;

		glm::vec3& GetAlbedoColor();
		void SetAlbedoColor(const glm::vec3& color);

		float& GetMetalness();
		void SetMetalness(float value);

		float& GetRoughness();
		void SetRoughness(float value);

		float& GetEmission();
		void SetEmission(float value);

		// Textures
		Ref<Texture2D> GetAlbedoMap();
		void SetAlbedoMap(Ref<Texture2D> texture);
		bool IsUsingAlbedoMap();
		void ClearAlbedoMap();

		Ref<Texture2D> GetNormalMap();
		void SetNormalMap(Ref<Texture2D> texture);
		bool IsUsingNormalMap();
		void SetUseNormalMap(bool value);
		void ClearNormalMap();

		Ref<Texture2D> GetMetalnessMap();
		void SetMetalnessMap(Ref<Texture2D> texture);
		bool IsUsingMetalnessMap();
		void ClearMetalnessMap();

		Ref<Texture2D> GetRoughnessMap();
		void SetRoughnessMap(Ref<Texture2D> texture);
		bool IsUsingRoughnessMap();
		void ClearRoughnessMap();

		static AssetType GetStaticType() { return AssetType::Material; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }

		Ref<Material> GetMaterial() const { return m_Material; }
	private:
		Ref<Material> m_Material;

	};
	/**/
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


