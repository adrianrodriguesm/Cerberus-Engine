#include "pch.h"
#include "Descriptor.h"
#include "Renderer/Renderer.h"
#include "Renderer/DeferredResourceDeletionQueue.h"
namespace cerberus
{
	DescriptorAllocator::DescriptorAllocator(const DescriptorAllocatorSpecification& spec)
		: m_Specification(spec)
	{
		std::vector<VkDescriptorPoolSize> sizes;
		sizes.reserve(m_Specification.PoolSizes.size());

		for (auto& [type, multiplier] : m_Specification.PoolSizes)
			sizes.push_back({ type, uint32_t(multiplier * m_Specification.MaxSets) });

		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		// Persistent sets are individually released by BindGroup. Transient sets
		// are reclaimed in bulk when their frame pool is reset.
		pool_info.flags = m_Specification.IsPersistent
			? m_Specification.Flags
			: 0;

		pool_info.maxSets = m_Specification.MaxSets;
		pool_info.poolSizeCount = (uint32_t)sizes.size();
		pool_info.pPoolSizes = sizes.data();

		m_AllocationCount.resize(m_Specification.NumberOfFrames);
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		m_DescriptorPools.resize(m_Specification.NumberOfFrames);
		for (size_t i = 0; i < m_Specification.NumberOfFrames; i++)
			vkCreateDescriptorPool(device, &pool_info, nullptr, &m_DescriptorPools[i]);
	}

	DescriptorAllocator::~DescriptorAllocator()
	{
		Release();
	}

	void DescriptorAllocator::Release()
	{
		if (m_DescriptorPools.empty())
			return;

		DeferredResourceDeletionQueue::SubmitCurrentFrame([descriptorPools = m_DescriptorPools]() mutable
		{
			VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();

			for (VkDescriptorPool descriptorPool : descriptorPools)
			{
				if (descriptorPool != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(device, descriptorPool, nullptr);
			}
		});

		m_DescriptorPools.clear();
		m_PersistentAllocations.clear();
	}

	void DescriptorAllocator::ResetPool()
	{
		// Reset descriptor pools here
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		uint32_t bufferIndex = Renderer::GetCurrentFrameIndex();
		vkResetDescriptorPool(device, m_DescriptorPools[bufferIndex], 0);
		m_AllocationCount[bufferIndex] = 0;
	}

	VkDescriptorSet DescriptorAllocator::Allocate(VkDescriptorSetLayout& layout)
	{
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		uint32_t bufferIndex = Renderer::GetCurrentFrameIndex();
		allocInfo.descriptorPool = m_DescriptorPools[bufferIndex];
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		const VkResult allocationResult = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
		if (allocationResult != VK_SUCCESS)
		{
			VulkanCheckResult(allocationResult);
			return VK_NULL_HANDLE;
		}

		++m_AllocationCount[bufferIndex];
		if (m_Specification.IsPersistent)
			m_PersistentAllocations[descriptorSet] = { m_DescriptorPools[bufferIndex], bufferIndex };

		return descriptorSet;
	}

	void DescriptorAllocator::Free(VkDescriptorSet descriptorSet)
	{
		if (descriptorSet == VK_NULL_HANDLE || !m_Specification.IsPersistent)
			return;

		auto allocation = m_PersistentAllocations.find(descriptorSet);
		if (allocation == m_PersistentAllocations.end())
			return;

		const VkDescriptorPool pool = allocation->second.Pool;
		const uint32_t poolIndex = allocation->second.PoolIndex;
		m_PersistentAllocations.erase(allocation);
		if (poolIndex < m_AllocationCount.size() && m_AllocationCount[poolIndex] > 0)
			--m_AllocationCount[poolIndex];

		DeferredResourceDeletionQueue::SubmitCurrentFrame([pool, descriptorSet]()
		{
			VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			VK_CHECK_RESULT(vkFreeDescriptorSets(device, pool, 1, &descriptorSet));
		});
	}

	DescriptorLayoutCache::DescriptorLayoutCache()
	{
	}
	DescriptorLayoutCache::~DescriptorLayoutCache()
	{
		if (m_DescriptoLayoutCache.empty())
			return;

		std::vector<VkDescriptorSetLayout> layouts;
		layouts.reserve(m_DescriptoLayoutCache.size());

		for (auto& [info, layout] : m_DescriptoLayoutCache)
		{
			if (layout != VK_NULL_HANDLE)
				layouts.push_back(layout);
		}

		DeferredResourceDeletionQueue::SubmitCurrentFrame([layouts = std::move(layouts)]() mutable
		{
			VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();

			for (VkDescriptorSetLayout layout : layouts)
			{
				if (layout != VK_NULL_HANDLE)
					vkDestroyDescriptorSetLayout(device, layout, nullptr);
			}
		});

		m_DescriptoLayoutCache.clear();
	}
	VkDescriptorSetLayout DescriptorLayoutCache::CreateDescriptorLayout(VkDescriptorSetLayoutCreateInfo* info)
	{
		DescriptorLayoutInfo layoutinfo;
		layoutinfo.Bindings.reserve(info->bindingCount);

		bool isSorted = true;
		int lastBinding = -1;

		// Copy from the direct info struct into our own one
		for (int i = 0; i < info->bindingCount; i++)
		{
			layoutinfo.Bindings.push_back(info->pBindings[i]);
			// Check that the bindings are in strict increasing order
			if (info->pBindings[i].binding > lastBinding)
				lastBinding = info->pBindings[i].binding;
			else
				isSorted = false;
		}
		// Sort the bindings if they aren't in order
		// NOTE: We want the bindings ordered because it  
		//		 makes equality check work much better
		if (!isSorted)
		{
			std::sort(layoutinfo.Bindings.begin(), layoutinfo.Bindings.end(), [](VkDescriptorSetLayoutBinding& a, VkDescriptorSetLayoutBinding& b)
			{
				return a.binding < b.binding;
			});
		}

		// Try to grab from cache
		if (m_DescriptoLayoutCache.find(layoutinfo) != m_DescriptoLayoutCache.end())
			return m_DescriptoLayoutCache[layoutinfo];
		
		// Otherwise create a new one (not found)
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		VkDescriptorSetLayout layout;
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, info, nullptr, &layout));

		// Add to cache
		m_DescriptoLayoutCache[layoutinfo] = layout;
		return layout;
	}

	bool DescriptorLayoutInfo::operator==(const DescriptorLayoutInfo& other) const
	{
		if (other.Bindings.size() != Bindings.size()) 
			return false;
		
		// Compare each of the bindings is the same. Bindings are sorted so they will match
		for (int i = 0; i < Bindings.size(); i++)
		{
			if (other.Bindings[i].binding != Bindings[i].binding)
				return false;
			if (other.Bindings[i].descriptorType != Bindings[i].descriptorType)
				return false;
			if (other.Bindings[i].descriptorCount != Bindings[i].descriptorCount)
				return false;
			if (other.Bindings[i].stageFlags != Bindings[i].stageFlags)
				return false;
		}
		return true;
	}

	size_t DescriptorLayoutInfo::Hash() const
	{
		size_t result = std::hash<size_t>()(Bindings.size());

		for (const VkDescriptorSetLayoutBinding& b : Bindings)
		{
			//pack the binding data into a single int64. Not fully correct but it's ok
			size_t binding_hash = b.binding | b.descriptorType << 8 | b.descriptorCount << 16 | b.stageFlags << 24;

			//shuffle the packed binding data and xor it with the main hash
			result ^= std::hash<size_t>()(binding_hash);
		}

		return result;
	}
}
