#pragma once
namespace cerberus
{
	///////////////////////////////////////////////////////////////////////
	//////// DescriptorAllocator
	///////////////////////////////////////////////////////////////////////
	struct DescriptorAllocatorSpecification
	{
		uint32_t NumberOfFrames;
		uint32_t MaxSets;
		VkDescriptorPoolCreateFlags Flags;
		bool IsPersistent = false;
		std::unordered_map<VkDescriptorType, float> PoolSizes = 
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.f },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.f },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1.f },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.f },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.5f }
		};
	};

	class DescriptorAllocator
	{
	public:
		DescriptorAllocator() = default;
		DescriptorAllocator(const DescriptorAllocatorSpecification& spec);
		~DescriptorAllocator();

		void Release();

		void ResetPool();
		VkDescriptorSet Allocate(VkDescriptorSetLayout& layout);

		const uint32_t GetAllocationCount(uint32_t index) const { return m_AllocationCount[index]; }

	private:
		DescriptorAllocatorSpecification m_Specification;
		std::vector<VkDescriptorPool> m_DescriptorPools;
		std::vector<uint32_t> m_AllocationCount;
	};
	///////////////////////////////////////////////////////////////////////
	//////// DescriptorLayout
	///////////////////////////////////////////////////////////////////////
	struct DescriptorLayoutInfo 
	{
		std::vector<VkDescriptorSetLayoutBinding> Bindings;

		bool operator==(const DescriptorLayoutInfo& other) const;

		size_t Hash() const;
	};

	struct DescriptorLayoutHash 
	{
		std::size_t operator()(const DescriptorLayoutInfo& layoutInfo) const { return layoutInfo.Hash(); }
	};

	class DescriptorLayoutCache
	{
	public:
		DescriptorLayoutCache();
		~DescriptorLayoutCache();

		VkDescriptorSetLayout CreateDescriptorLayout(VkDescriptorSetLayoutCreateInfo* info);
	private:
		std::unordered_map<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> m_DescriptoLayoutCache;
	};

}
