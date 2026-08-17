#include "pch.h"
#include "Material.h"
#include "Renderer/RendererResources.h"
#include "Renderer/Renderer.h"
#include "Renderer/DescriptorManager.h"
namespace cerberus
{
	static const std::string s_AlbedoColorUniform = "u_MaterialUniforms.AlbedoColor";
	static const std::string s_UseNormalMapUniform = "u_MaterialUniforms.UseNormalMap";
	static const std::string s_MetalnessUniform = "u_MaterialUniforms.Metalness";
	static const std::string s_RoughnessUniform = "u_MaterialUniforms.Roughness";
	static const std::string s_EmissionUniform = "u_MaterialUniforms.Emission";

	static const std::string s_AlbedoMapUniform = "u_AlbedoTex";
	static const std::string s_NormalMapUniform = "u_NormalTex";
	static const std::string s_MetalnessMapUniform = "u_MetallicTex";
	static const std::string s_RoughnessMapUniform = "u_RoughnessTex";

	Material::Material(const Ref<Shader>& shader, const std::string& name, bool persistent)
		: m_Shader(shader), m_Name(name), m_WriteDescriptors(Renderer::GetRendererConfig().FramesInFlight),
		m_DescriptorSets(Renderer::GetRendererConfig().FramesInFlight), m_Persistent(persistent)
	{
		for(auto& descriptor : m_DescriptorSets)
			descriptor = VK_NULL_HANDLE;
		
		AllocateStorage();
	}
	Material::~Material()
	{
		m_UniformBufferStorage.Release();
		m_DescriptorSets.clear();
		m_WriteDescriptors.clear();
		m_ResidentDescriptors.clear();
		m_Textures.clear();
	}

	void Material::UpdateForRender(const std::vector<std::vector<std::vector<VkWriteDescriptorSet>>>& uniformBufferWriteDescriptors)
	{
		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		/**/
		// We can't cache the results atm because we might render the same material 
		// in different viewports, and so we can't bind to the same uniform buffers
		m_WriteDescriptors[frameIndex].clear();
		/**/
		/////////////////////////////////////
		// Get VkWriteDescriptor of each type
		/////////////////////////////////////
		// Uniform Buffer
		if (!uniformBufferWriteDescriptors.empty())
		{
			// Update VkWriteDescripor List
			for (auto& writeDescriptor : uniformBufferWriteDescriptors[frameIndex][0])
				m_WriteDescriptors[frameIndex].push_back(writeDescriptor); 
		}
		// Resources
		for (auto&& [binding, pendingDescriptor] : m_ResidentDescriptors)
		{
			// Update VkWriteDescripor List
			m_WriteDescriptors[frameIndex].push_back(pendingDescriptor->WriteDescriptorSet);
		}

		auto& descriptorSetLayouts = m_Shader->GetDescriptorSetLayouts();
		//const bool persistent = m_Persistent;
		const bool persistent = false;
		if ((!persistent || m_DescriptorSets[frameIndex] == VK_NULL_HANDLE) && descriptorSetLayouts.size())
		{
			VkDescriptorSetLayout descriptorSetLayout = descriptorSetLayouts[0];
			m_DescriptorSets[frameIndex] = DescriptorManager::AllocateDescriptorSet(descriptorSetLayout, persistent);

			CORE_ASSERT(m_DescriptorSets[frameIndex], "Invalid descriptor set created!");
			//m_DescriptorSets[frameIndex] = descriptorSet;
			for (auto& writeDescriptor : m_WriteDescriptors[frameIndex])
				writeDescriptor.dstSet = m_DescriptorSets[frameIndex];

			vkUpdateDescriptorSets(vulkanDevice, (uint32_t)m_WriteDescriptors[frameIndex].size(), m_WriteDescriptors[frameIndex].data(), 0, nullptr);
		}



	}

	void Material::Set(const std::string& name, float value)
	{
		Set<float>(name, value);
	}
	void Material::Set(const std::string& name, int value)
	{
		Set<int>(name, value);
	}
	void Material::Set(const std::string& name, uint32_t value)
	{
		Set<uint32_t>(name, value);
	}
	void Material::Set(const std::string& name, bool value)
	{
		Set<int>(name, value);
	}
	void Material::Set(const std::string& name, const glm::ivec2& value)
	{
		Set<glm::ivec2>(name, value);
	}
	void Material::Set(const std::string& name, const glm::ivec3& value)
	{
		Set<glm::ivec3>(name, value);
	}
	void Material::Set(const std::string& name, const glm::ivec4& value)
	{
		Set<glm::ivec4>(name, value);
	}
	void Material::Set(const std::string& name, const glm::vec2& value)
	{
		Set<glm::vec2>(name, value);
	}
	void Material::Set(const std::string& name, const glm::vec3& value)
	{
		Set<glm::vec3>(name, value);
	}
	void Material::Set(const std::string& name, const glm::vec4& value)
	{
		Set<glm::vec4>(name, value);
	}
	void Material::Set(const std::string& name, const glm::mat3& value)
	{
		Set<glm::mat3>(name, value);
	}
	void Material::Set(const std::string& name, const glm::mat4& value)
	{
		Set<glm::mat4>(name, value);
	}

	void Material::Set(const std::string& name, const Ref<Image>& image)
	{
#if OLD_RENDERER
		const ShaderResourceDeclaration* resource = FindResourceDeclaration(name);
		CORE_ASSERT(resource, "Resource does not exist!");

		uint32_t binding = resource->GetRegister();
		// Texture is already set
		if (binding < m_Images.size() && m_Images[binding] && image->GetHash() == m_Images[binding]->GetHash())
			return;

		if (binding >= m_Images.size())
			m_Images.resize(binding + 1);

		m_Images[binding] = image;

		const VkWriteDescriptorSet* writeDescriptorSet = m_Shader->GetWriteDescriptorSet(name, 0);
		CORE_ASSERT(writeDescriptorSet, "Descriptor set not found!");
		m_ResidentDescriptors[binding] = std::make_shared<PendingDescriptor>(PendingDescriptor{ PendingDescriptorType::Image2D, *writeDescriptorSet, nullptr, image });
		//image->UpdateDescriptor();
		m_ResidentDescriptors[binding]->WriteDescriptorSet.pImageInfo = &image->GetVulkanDescriptorInfo();
#endif
	}
	void Material::Set(const std::string& name, const Handle<TextureCube>& texture)
	{
#if OLD_RENDERER
		const ShaderResourceDeclaration* resource = FindResourceDeclaration(name);
		CORE_ASSERT(resource, "Resource does not exist!");

		uint32_t binding = resource->GetRegister();
		// Texture is already set
		if (binding < m_Textures.size() && m_Textures[binding] && texture->GetHash() == m_Textures[binding]->GetHash())
			return;

		if (binding >= m_Textures.size())
			m_Textures.resize(binding + 1);

		m_Textures[binding] = texture;

		const VkWriteDescriptorSet* writeDescriptorSet = m_Shader->GetWriteDescriptorSet(name, 0);
		CORE_ASSERT(writeDescriptorSet, "Descriptor set not found!");
		m_ResidentDescriptors[binding] = std::make_shared<PendingDescriptor>(PendingDescriptor{ PendingDescriptorType::TextureCube, *writeDescriptorSet,  std::static_pointer_cast<Texture>(texture) });
		m_ResidentDescriptors[binding]->WriteDescriptorSet.pImageInfo = &texture->GetVulkanDescriptorInfo();
#endif
	}
	void Material::AllocateStorage()
	{
		const auto& shaderBuffers = m_Shader->GetShaderBuffers();

		if (shaderBuffers.size() > 0)
		{
			uint32_t size = 0;
			for (auto [name, shaderBuffer] : shaderBuffers)
				size += shaderBuffer.Size;

			m_UniformBufferStorage.Allocate(size);
			m_UniformBufferStorage.ZeroInitialize();
		}
	}

	const ShaderUniform* Material::FindUniformDeclaration(const std::string& name)
	{
		const auto& shaderBuffers = m_Shader->GetShaderBuffers();

		CORE_ASSERT(shaderBuffers.size() <= 1, "We currently only support ONE material buffer!");

		if (shaderBuffers.size() > 0)
		{
			const ShaderBuffer& buffer = (*shaderBuffers.begin()).second;
			if (buffer.Uniforms.find(name) == buffer.Uniforms.end())
				return nullptr;

			return &buffer.Uniforms.at(name);
		}
		return nullptr;
	}
	const ShaderResourceDeclaration* Material::FindResourceDeclaration(const std::string& name)
	{
		const auto& shaderResources = m_Shader->GetResources();
		for (const auto& [n, resource] : shaderResources)
		{
			if (resource.GetName() == name)
				return &resource;
		}
		return nullptr;
	}
	float& Material::GetFloat(const std::string& name)
	{
		return Get<float>(name);
	}
	int32_t& Material::GetInt(const std::string& name)
	{
		return Get<int>(name);
	}
	uint32_t& Material::GetUInt(const std::string& name)
	{
		return Get<uint32_t>(name);
	}
	bool& Material::GetBool(const std::string& name)
	{
		return Get<bool>(name);
	}
	glm::vec2& Material::GetVector2(const std::string& name)
	{
		return Get<glm::vec2>(name);
	}
	glm::vec3& Material::GetVector3(const std::string& name)
	{
		return Get<glm::vec3>(name);
	}
	glm::vec4& Material::GetVector4(const std::string& name)
	{
		return Get<glm::vec4>(name);
	}
	glm::mat3& Material::GetMatrix3(const std::string& name)
	{
		return Get<glm::mat3>(name);
	}
	glm::mat4& Material::GetMatrix4(const std::string& name)
	{
		return Get<glm::mat4>(name);
	}

	Ref<TextureCube> Material::GetTextureCube(const std::string& name)
	{
		return GetResource<TextureCube>(name);
	}

	Ref<TextureCube> Material::TryGetTextureCube(const std::string& name)
	{
		return TryGetResource<TextureCube>(name);
	}

	MaterialAsset::MaterialAsset()
	{
		m_Material = CreateRef<Material>(RendererResources::GetShaderLibrary().Get("PBR"));
		ApplyDataToMaterial();
	}

	MaterialAsset::MaterialAsset(Ref<Material> material)
		: m_Material(material)
	{
		ApplyDataToMaterial();
	}

	MaterialAsset::~MaterialAsset()
	{
	}

	void MaterialAsset::ApplyDataToMaterial()
	{
		if (!m_Material)
			return;

		m_Material->Set(s_AlbedoColorUniform, m_Data.AlbedoColor);
		m_Material->Set(s_UseNormalMapUniform, m_Data.UseNormalMap);
		m_Material->Set(s_MetalnessUniform, m_Data.Metalness);
		m_Material->Set(s_RoughnessUniform, m_Data.Roughness);
		m_Material->Set(s_EmissionUniform, m_Data.Emission);
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

	const std::string& MaterialAsset::GetName() const
	{
		return m_Material->GetName();
	}

	glm::vec3& MaterialAsset::GetAlbedoColor()
	{
		return m_Data.AlbedoColor;
	}

	void MaterialAsset::SetAlbedoColor(const glm::vec3& color)
	{
		m_Data.AlbedoColor = color;
		if (m_Material)
			m_Material->Set(s_AlbedoColorUniform, color);
	}

	float& MaterialAsset::GetMetalness()
	{
		return m_Data.Metalness;
	}

	void MaterialAsset::SetMetalness(float value)
	{
		m_Data.Metalness = value;
		if (m_Material)
			m_Material->Set(s_MetalnessUniform, value);
	}

	float& MaterialAsset::GetRoughness()
	{
		return m_Data.Roughness;
	}

	void MaterialAsset::SetRoughness(float value)
	{
		m_Data.Roughness = value;
		if (m_Material)
			m_Material->Set(s_RoughnessUniform, value);
	}

	float& MaterialAsset::GetEmission()
	{
		return m_Data.Emission;
	}

	void MaterialAsset::SetEmission(float value)
	{
		m_Data.Emission = value;
		if (m_Material)
			m_Material->Set(s_EmissionUniform, value);
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
		if (m_Material)
			m_Material->Set(s_UseNormalMapUniform, m_Data.UseNormalMap);
	}

	bool MaterialAsset::IsUsingNormalMap() const
	{
		return m_Data.UseNormalMap && m_Data.Textures.HasNormalMap();
	}

	void MaterialAsset::SetUseNormalMap(bool value)
	{
		m_Data.UseNormalMap = value;
		if (m_Material)
			m_Material->Set(s_UseNormalMapUniform, value);
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
