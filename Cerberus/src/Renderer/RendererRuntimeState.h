#pragma once

#include "Renderer/RendererContext.h"
#include "Renderer/CommandBuffer.h"
#include "Renderer/UniformBufferSet.h"
#include "Renderer/SceneRenderer/SceneRenderer.h"

namespace cerberus
{
	// Global renderer state grouped by ownership/responsibility.
	// This keeps Renderer as the public facade while making it explicit which
	// subsystem owns frame state, command resources, descriptors and UI.
	struct RendererCommandState
	{
		VkCommandPool GraphicsCommandPool = VK_NULL_HANDLE;
		VkCommandPool ComputeCommandPool = VK_NULL_HANDLE;

		CommandBuffer* MainCommandBuffer = nullptr;
		CommandBuffer* SwapchainCommandBuffer = nullptr;
	};

	struct RendererDescriptorState
	{
		// Descriptor allocation/layout ownership lives in DescriptorManager.
		// This state only stores renderer-level descriptor caches.
		std::unordered_map<SceneRenderer*, std::vector<std::vector<VkDescriptorSet>>> RendererDescriptorSetCache;
		VkDescriptorSet ActiveRendererDescriptorSet = VK_NULL_HANDLE;

		// UniformBufferSet -> Shader Hash -> Frame -> Set -> WriteDescriptor
		std::unordered_map<UniformBufferSet*, std::unordered_map<uint64_t, std::vector<std::vector<std::vector<VkWriteDescriptorSet>>>>> UniformBufferWriteDescriptorCache;
	};

	struct RendererImGuiState
	{
		VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> SecondaryCommandBuffers;
	};

	struct RendererFrameState
	{
		VkPhysicalDeviceLimits DeviceLimits{};
		uint32_t DrawCallCount = 0;
	};
}
