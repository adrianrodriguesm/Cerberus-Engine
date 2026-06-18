#pragma once

#include "Renderer/Descriptor.h"
#include "Renderer/DescriptorAllocation.h"

namespace cerberus
{

	// DescriptorManager is the single owner of descriptor allocation state.
	//
	// Ownership rules:
	// - Renderer initializes/shuts down this system.
	// - ResourceManager owns BindGroup/BindGroupLayout wrappers, but does not own descriptor allocators.
	// - BindGroup allocates descriptor sets through this manager.
	// - Descriptor set layouts are cached/owned here and destroyed at renderer shutdown.
	class DescriptorManager
	{
	public:
		static void Init();
		static void Shutdown();

		// Resets only frame-local/transient descriptor pools.
		// Must be called once at the frame boundary, after the fence for the current frame index has been waited,
		// and before any transient descriptor set is allocated or command buffer is recorded for that frame.
		static void ResetTransientFramePools();

		// Backwards-compatible alias. Prefer ResetTransientFramePools() in new code.
		static void ResetFramePools();

		static VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout, DescriptorAllocationLifetime lifetime);
		static VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout, bool persistent = false);
		static VkDescriptorSet AllocateTransientDescriptorSet(VkDescriptorSetLayout layout);
		static VkDescriptorSet AllocatePersistentDescriptorSet(VkDescriptorSetLayout layout);
		static VkDescriptorSetLayout CreateDescriptorSetLayout(VkDescriptorSetLayoutCreateInfo* info);

		static DescriptorAllocator& GetTransientAllocator();
		static DescriptorAllocator& GetPersistentAllocator();
		static DescriptorLayoutCache& GetLayoutCache();

		static bool IsInitialized();
	};
}
