#pragma once
#include "Renderer/DescriptorAllocation.h"
#include "Core/Handle.h"
#include "Renderer/Shader.h"
#include "Renderer/UniformBufferSet.h"
#include "Renderer/Image.h"
namespace cerberus
{
	class AccelerationStructure;
	class BindGroupLayout
	{
	public:
		struct ASBinding
		{
			std::string Name = "";
			uint32_t BindingPoint = 0;
			VkShaderStageFlagBits ShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		};

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
			std::vector<ASBinding>     AccelerationStructures;
			std::vector<ImageBinding>  ImageSamplers;
			std::vector<ImageBinding>  ImageStorages;
			uint32_t Set = 0;

			bool operator==(const BindGroupLayoutInfo& other) const;
		};

	public:
		BindGroupLayout(const BindGroupLayoutInfo& info);
		BindGroupLayout(BindGroupLayout&& other) noexcept;
		BindGroupLayout(const BindGroupLayout&) = delete;
		BindGroupLayout& operator=(const BindGroupLayout&) = delete;
		BindGroupLayout& operator=(BindGroupLayout&&) = delete;
		~BindGroupLayout();

		const BindGroupLayoutInfo& GetLayoutInfo() const { return m_Info; }
		const VkDescriptorSetLayout GetVulkanDescriptorSetLayout() const { return m_DescriptorSetLayout; }

	private:
		BindGroupLayoutInfo m_Info;
		std::vector<std::vector<VkWriteDescriptorSet>> m_WriteDescriptors;
		VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;

		friend class BindGroup;
	};


	struct BindGroupSpecification
	{
		Handle<BindGroupLayout> BindGroupLayout;

		// BindGroups are engine/resource wrappers and should be persistent by default.
		// Use Transient only for descriptor sets that are rebuilt every frame and never reused across frames.
		DescriptorAllocationLifetime Lifetime = DescriptorAllocationLifetime::Persistent;
	};

	class BindGroup
	{
	public:
		BindGroup(const BindGroupSpecification& spec);
		~BindGroup();

		void SetUniformBuffer(const Ref<UniformBufferSet> uboSet, uint32_t bindingPoint);
		void SetImageView(const Handle<ImageView> imgView, uint32_t bindingPoint);
		void SetAccelerationStructure(const Handle<AccelerationStructure> as, uint32_t bindingPoint);

		void Update(uint32_t frameIndex);
		void Update();

		VkDescriptorSet GetHandle(uint32_t frameIndex);
	private: 
		struct AccelerationStructureDescriptorState
		{
			VkAccelerationStructureKHR AccelerationStructure = VK_NULL_HANDLE;
			VkWriteDescriptorSetAccelerationStructureKHR WriteInfo{};
		};

		std::vector<std::vector<AccelerationStructureDescriptorState>> m_AccelerationStructureDescriptorStates;
	private:
		Handle<BindGroupLayout> m_BindGroupLayout;
		std::vector<VkDescriptorSet> m_Descriptors;
		// FrameIndex -> [Binding Point, WriteDescriptor]
		std::vector<std::vector<VkWriteDescriptorSet>> m_WriteDescriptors;
		DescriptorAllocationLifetime m_Lifetime = DescriptorAllocationLifetime::Persistent;
	};

	/** /
	// 0 -> Global Perisistent data (env map)
	// 1 -> Global dynamic data (once per frame) (e.g shadow map, camera)
	// 2 -> Global dynamic data per render pass (once per frame)
	// 3 -> material data (once per draw)
	ShaderDescriptorSetData descData = Shader->GetShaderDescriptotSetData(3);
	// this function then creates or gets the layout a build the  BindGroup
	Handle<BindGroup> BindGroup = ResourceManager::CreateBindGroup(material, descData);
	ResourceManager::UpdateBindGroup(BindGroup);
	/**/

}