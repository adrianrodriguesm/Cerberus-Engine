#pragma once
#include "Core/Handle.h"
#include "Renderer/Shader.h"
#include "Renderer/UniformBufferSet.h"
namespace cerberus
{
	class BindGroupLayout
	{
	public:
		struct BufferBinding
		{
			std::string Name = "";
			uint32_t BindingPoint = 0;
			VkShaderStageFlagBits ShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
			BufferUsageType UsageType = BufferUsageType::Default;
		};

		struct ImageBinding
		{
			std::string Name = "";
			uint32_t BindingPoint = 0;
			uint32_t ArraySize = 0;
			VkShaderStageFlagBits ShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		};

		struct BindGroupLayoutInfo
		{
			std::vector<BufferBinding> UniformBuffers;
			std::vector<BufferBinding> StorageBuffers;
			std::vector<ImageBinding> ImageSamplers;
			std::vector<ImageBinding> ImageStorages;
			uint32_t Set = 0;

			bool operator==(const BindGroupLayoutInfo& other) const;
		};

	public:
		BindGroupLayout(const BindGroupLayoutInfo& info);
		~BindGroupLayout();

		const BindGroupLayoutInfo& GetLayoutInfo() const { return m_Info; }
		const VkDescriptorSetLayout GetVulkanDescriptorSetLayout() const { return m_DescriptorSetLayout; }

	private:
		BindGroupLayoutInfo m_Info;
		std::vector<std::vector<VkWriteDescriptorSet>> m_WriteDescriptors;
		VkDescriptorSetLayout m_DescriptorSetLayout;

		friend class BindGroup;
	};


	struct BindGroupSpecification
	{
		Handle<BindGroupLayout> BindGroupLayout;
		bool Persistent;
	};

	class BindGroup
	{
	public:
		BindGroup(const BindGroupSpecification& spec);
		~BindGroup();

		void SetUniformBuffer(const Ref<UniformBufferSet> uboSet, uint32_t bindingPoint);

		void Update(uint32_t frameIndex);
		void Update();
	private:
		Handle<BindGroupLayout> m_BindGroupLayout;
		std::vector<VkDescriptorSet> m_Descriptors;
		// Binding Point -> FrameIndex -> WriteDescriptor
		std::map<uint32_t, std::vector<VkWriteDescriptorSet>> m_WriteDescriptors;
	};

	/** /
	// 0 -> Global Perisistent data (env map)
	// 1 -> Global dynamic data (once per frame) (e.g shadow map, camera)
	// 2 -> material data (once per draw)
	ShaderDescriptorSetData descData = Shader->GetShaderDescriptotSetData(3);
	// this function then creates or gets the layout a build the  BindGroup
	Handle<BindGroup> BindGroup = ResourceManager::CreateBindGroup(material, descData);
	ResourceManager::UpdateBindGroup(BindGroup);
	/**/

}