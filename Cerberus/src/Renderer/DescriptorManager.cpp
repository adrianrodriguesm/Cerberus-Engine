#include "pch.h"
#include "DescriptorManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/DeferredResourceDeletionQueue.h"

namespace cerberus
{
	struct DescriptorManagerData
	{
		DescriptorAllocator* TransientAllocator = nullptr;
		DescriptorAllocator* PersistentAllocator = nullptr;
		DescriptorLayoutCache* LayoutCache = nullptr;
	};

	static DescriptorManagerData* s_DescriptorManagerData = nullptr;

	void DescriptorManager::Init()
	{
		CORE_ASSERT(!s_DescriptorManagerData, "DescriptorManager is already initialized");

		s_DescriptorManagerData = new DescriptorManagerData();

		DescriptorAllocatorSpecification descriptorAllocatorSpec;
		descriptorAllocatorSpec.NumberOfFrames = Renderer::GetRendererConfig().FramesInFlight;
		descriptorAllocatorSpec.MaxSets = 1000;
		descriptorAllocatorSpec.Flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		descriptorAllocatorSpec.IsPersistent = false;

		s_DescriptorManagerData->TransientAllocator = new DescriptorAllocator(descriptorAllocatorSpec);

		descriptorAllocatorSpec.IsPersistent = true;
		s_DescriptorManagerData->PersistentAllocator = new DescriptorAllocator(descriptorAllocatorSpec);

		s_DescriptorManagerData->LayoutCache = new DescriptorLayoutCache();
	}

	void DescriptorManager::Shutdown()
	{
		if (!s_DescriptorManagerData)
			return;

		delete s_DescriptorManagerData->TransientAllocator;
		s_DescriptorManagerData->TransientAllocator = nullptr;

		delete s_DescriptorManagerData->PersistentAllocator;
		s_DescriptorManagerData->PersistentAllocator = nullptr;

		delete s_DescriptorManagerData->LayoutCache;
		s_DescriptorManagerData->LayoutCache = nullptr;

		// DescriptorAllocator/DescriptorLayoutCache destructors submit native Vulkan
		// destruction work. Drain it while the VkDevice is still alive.
		DeferredResourceDeletionQueue::FlushAll();

		delete s_DescriptorManagerData;
		s_DescriptorManagerData = nullptr;
	}

	void DescriptorManager::ResetTransientFramePools()
	{
		if (!s_DescriptorManagerData || !s_DescriptorManagerData->TransientAllocator)
			return;

		s_DescriptorManagerData->TransientAllocator->ResetPool();
	}

	VkDescriptorSet DescriptorManager::AllocateDescriptorSet(VkDescriptorSetLayout layout, DescriptorAllocationLifetime lifetime)
	{
		CORE_ASSERT(s_DescriptorManagerData, "DescriptorManager is not initialized");

		DescriptorAllocator* allocator = nullptr;
		switch (lifetime)
		{
		case DescriptorAllocationLifetime::Transient:
			allocator = s_DescriptorManagerData->TransientAllocator;
			break;
		case DescriptorAllocationLifetime::Persistent:
			allocator = s_DescriptorManagerData->PersistentAllocator;
			break;
		}

		CORE_ASSERT(allocator, "Invalid descriptor allocator");
		return allocator->Allocate(layout);
	}

	VkDescriptorSetLayout DescriptorManager::CreateDescriptorSetLayout(VkDescriptorSetLayoutCreateInfo* info)
	{
		CORE_ASSERT(s_DescriptorManagerData, "DescriptorManager is not initialized");
		CORE_ASSERT(s_DescriptorManagerData->LayoutCache, "Invalid descriptor layout cache");
		return s_DescriptorManagerData->LayoutCache->CreateDescriptorLayout(info);
	}

	bool DescriptorManager::IsInitialized()
	{
		return s_DescriptorManagerData != nullptr;
	}
}
