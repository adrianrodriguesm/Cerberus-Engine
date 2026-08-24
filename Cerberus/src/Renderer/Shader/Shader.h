#pragma once
#include "Renderer/RHI/Buffers/UniformBuffer.h"
namespace cerberus
{
	class ShaderResourceDeclaration
	{
	public:
		ShaderResourceDeclaration() = default;
		ShaderResourceDeclaration(const std::string& name, uint32_t resourceRegister, uint32_t count)
			: m_Name(name), m_Register(resourceRegister), m_Count(count) {}

		virtual const std::string& GetName() const { return m_Name; }
		virtual uint32_t GetRegister() const { return m_Register; }
		virtual uint32_t GetCount() const { return m_Count; }
	private:
		std::string m_Name;
		uint32_t m_Register = 0;
		uint32_t m_Count = 0;
	};
	
	enum class ShaderUniformType
	{
		None = 0, Bool, Int, UInt, Float, Vec2, Vec3, Vec4, Mat3, Mat4,
		IVec2, IVec3, IVec4
	};

	class ShaderUniform
	{
	public:
		ShaderUniform() = default;
		ShaderUniform(std::string name, ShaderUniformType type, uint32_t size, uint32_t offset);

		const std::string& GetName() const { return m_Name; }
		ShaderUniformType GetType() const { return m_Type; }
		uint32_t GetSize() const { return m_Size; }
		uint32_t GetOffset() const { return m_Offset; }

		static const std::string& UniformTypeToString(ShaderUniformType type);
	private:
		std::string m_Name;
		ShaderUniformType m_Type = ShaderUniformType::None;
		uint32_t m_Size = 0;
		uint32_t m_Offset = 0;
	};

	struct ShaderUniformBuffer
	{
		std::string Name;
		uint32_t Index;
		uint32_t BindingPoint;
		uint32_t Size;
		uint32_t RendererID;
		std::vector<ShaderUniform> Uniforms;
	};

	struct ShaderStorageBuffer
	{
		std::string Name;
		uint32_t Index;
		uint32_t BindingPoint;
		uint32_t Size;
		uint32_t RendererID;
		//std::vector<ShaderUniform> Uniforms;
	};
	// PushConstants
	struct ShaderBuffer
	{
		std::string Name;
		uint32_t Size = 0;
		std::unordered_map<std::string, ShaderUniform> Uniforms;
	};


	struct ShaderStageSource
	{
		VkShaderStageFlagBits Stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		std::string FilePath;
	};

	struct ShaderSpecification
	{
		std::string Name;
		std::vector<ShaderStageSource> Stages;
	};


	class Shader
	{
	public:
		struct UniformBuffer
		{
			VkDescriptorBufferInfo Descriptor;
			uint32_t Size = 0;
			uint32_t BindingPoint = 0;
			std::string Name;
			VkShaderStageFlagBits ShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
			BufferUsageType UsageType = BufferUsageType::Default;
		};

		struct StorageBuffer
		{
			VkDescriptorBufferInfo Descriptor;
			uint32_t Size = 0;
			uint32_t BindingPoint = 0;
			std::string Name;
			VkShaderStageFlagBits ShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
			BufferUsageType UsageType = BufferUsageType::Default;
		};

		struct ImageSampler
		{
			uint32_t BindingPoint = 0;
			uint32_t DescriptorSet = 0;
			uint32_t ArraySize = 0;
			std::string Name;
			VkShaderStageFlagBits ShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		};

		struct AccelerationStructure
		{
			uint32_t BindingPoint = 0;
			uint32_t DescriptorSet = 0;
			std::string Name;
			VkShaderStageFlagBits ShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		};

		struct ShaderDescriptorSet
		{
			std::unordered_map<uint32_t, Shader::UniformBuffer*> UniformBuffers;
			std::unordered_map<uint32_t, Shader::StorageBuffer*> StorageBuffers;
			std::unordered_map<uint32_t, Shader::AccelerationStructure*> AccelerationStructures;
			
			std::unordered_map<uint32_t, Shader::ImageSampler> ImageSamplers;
			std::unordered_map<uint32_t, Shader::ImageSampler> StorageImages;
			
			// VkWriteDescriptorSet descripte how the buffer write to the descriptor set
			std::unordered_map<std::string, VkWriteDescriptorSet> WriteDescriptorSets;

			operator bool() const { return !(UniformBuffers.empty() && StorageBuffers.empty() && ImageSamplers.empty() && StorageImages.empty() && AccelerationStructures.empty()); }
		};
	public:
		Shader(const std::string& path, bool forceCompile = false);
		Shader(const ShaderSpecification& specification, bool forceCompile = false);
		~Shader();
		static void ClearUniformBuffers();
		
		bool HasDescriptorSet(uint32_t set) const { return m_TypeCounts.find(set) != m_TypeCounts.end(); }

		void SubmitResourcesToFree();
		void Reload(bool forceCompile = false);
		const size_t GetHash() const;
		const std::string& GetName() const { return  m_Name; }
		const std::unordered_map<VkShaderStageFlagBits, std::vector<uint32_t >>& GetShaderByteCode() const { return m_ShaderByteCodeData; }
		const std::unordered_map<std::string, ShaderBuffer>& GetShaderBuffers() const  { return m_Buffers; }
		const std::unordered_map<std::string, ShaderResourceDeclaration>& GetResources() const { return m_Resources; }
		const std::vector<VkPushConstantRange>& GetVulkanPushConstantRanges() const { return m_VulkanPushConstantRanges; }
		
		const std::vector<VkDescriptorSetLayout>& GetDescriptorSetLayouts() const { return m_DescriptorSetLayouts; }
		VkDescriptorSetLayout& GetDescriptorSetLayout(uint32_t set)  { CORE_ASSERT(set < m_DescriptorSetLayouts.size(), "") return m_DescriptorSetLayouts[set]; }
		const std::vector<VkPipelineShaderStageCreateInfo> GetPipelineShaderModules() const { return m_PipelineShaderStageCreateInfos; }
		const std::vector<ShaderDescriptorSet>& GetShaderDescriptorSets() const { return m_ShaderDescriptorSets; }
		const VkWriteDescriptorSet* GetWriteDescriptorSet(const std::string& name, uint32_t set) const;
	private:
		std::unordered_map<VkShaderStageFlagBits, std::string> PreProcess(const std::string& source);
		std::string PreProcessStageSource(const std::string& source, VkShaderStageFlagBits stage, const std::string& sourcePath);
		// Compile or Get Binary shader cached
		void CompileOrGetVulkanBinary(std::unordered_map<VkShaderStageFlagBits, std::vector<uint32_t>>& outputBinary, bool forceCompile);
		// Create Shader Modules

		void LoadAndCreateShaders(const std::unordered_map<VkShaderStageFlagBits, std::vector<uint32_t>>& shaderData);
		// Cache the data from push constant and the 
		void Reflect(VkShaderStageFlagBits shaderStage, const std::vector<uint32_t>& shaderData);
		void ReflectAllShaderStages(const std::unordered_map<VkShaderStageFlagBits, std::vector<uint32_t>>& shaderData);

		void CreateDescriptors();
	private:
		std::vector<VkPipelineShaderStageCreateInfo> m_PipelineShaderStageCreateInfos;
		std::unordered_map<VkShaderStageFlagBits, std::string> m_ShaderSource;
		std::unordered_map<VkShaderStageFlagBits, std::string> m_ShaderSourceFilePaths;
		std::string m_AssetPath;
		std::string m_Name;

		std::vector<ShaderDescriptorSet> m_ShaderDescriptorSets;

		std::vector<VkPushConstantRange> m_VulkanPushConstantRanges;
		std::unordered_map<std::string, ShaderResourceDeclaration> m_Resources;

		std::unordered_map<std::string, ShaderBuffer> m_Buffers;


		std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;
		VkDescriptorSet m_DescriptorSet;
		//VkDescriptorPool m_DescriptorPool = nullptr;

		std::unordered_map<uint32_t, std::vector<VkDescriptorPoolSize>> m_TypeCounts;
		std::unordered_map<VkShaderStageFlagBits, std::vector<uint32_t >> m_ShaderByteCodeData;
	};

	class Renderer;
	class ShaderLibrary
	{
	public:
		ShaderLibrary();
		~ShaderLibrary();

		void ReleaseAllShaders();

		void Add(const Ref<Shader>& shader);
		void Load(const std::string& path, bool forceCompile = false);
		void Load(const std::string& name, const std::string& path);
		void Load(const ShaderSpecification& specification, bool forceCompile = false);
		bool Contains(const std::string& name) const;
		void Unload(const std::string& name);

		const Ref<Shader>& Get(const std::string& name) const;
	private:
		void SubmitResourceFree();
	private:
		std::unordered_map<std::string, Ref<Shader>> m_Shaders;
	};
}
