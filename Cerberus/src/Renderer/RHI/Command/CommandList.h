#pragma once

#include "Renderer/RHI/Command/CommandBufferView.h"
#include "Core/Handle.h"

#include <cstddef>
#include <span>
#include <type_traits>

namespace cerberus
{
	class BindGroup;

	struct RendererFrameStatistics
	{
		uint32_t DrawCallCount = 0;
		uint32_t ComputeDispatchCount = 0;
		uint32_t RayTracingDispatchCount = 0;
	};

	// Non-owning recording view over an already-begun Vulkan command buffer.
	// Allocation, begin/end, submission and synchronization belong to CommandBuffer
	// or TransientCommandExecutor.
	class CommandListBase
	{
	public:
		explicit CommandListBase(
			CommandBufferView commandBuffer,
			RendererFrameStatistics* statistics = nullptr);

		uint32_t GetFrameIndex() const { return m_CommandBuffer.GetFrameIndex(); }
		bool IsValid() const { return m_CommandBuffer.IsValid(); }

		void PipelineBarrier(
			VkPipelineStageFlags sourceStage,
			VkPipelineStageFlags destinationStage,
			VkDependencyFlags dependencyFlags = 0,
			std::span<const VkMemoryBarrier> memoryBarriers = {},
			std::span<const VkBufferMemoryBarrier> bufferBarriers = {},
			std::span<const VkImageMemoryBarrier> imageBarriers = {}) const;

		void PushConstants(
			VkPipelineLayout pipelineLayout,
			VkShaderStageFlags shaderStages,
			std::span<const std::byte> data,
			uint32_t offset = 0) const;

	protected:
		VkCommandBuffer GetNative() const { return m_CommandBuffer.GetNative(); }
		RendererFrameStatistics* GetStatistics() const { return m_Statistics; }

		void BindDescriptorSetInternal(
			VkPipelineBindPoint bindPoint,
			VkPipelineLayout pipelineLayout,
			Handle<BindGroup> bindGroup,
			uint32_t firstSet) const;
		void BindDescriptorSetsInternal(
			VkPipelineBindPoint bindPoint,
			VkPipelineLayout pipelineLayout,
			std::span<const Handle<BindGroup>> bindGroups,
			uint32_t firstSet) const;
		void BindDescriptorSetsInternal(
			VkPipelineBindPoint bindPoint,
			VkPipelineLayout pipelineLayout,
			std::span<const VkDescriptorSet> descriptorSets,
			uint32_t firstSet) const;

	private:
		CommandBufferView m_CommandBuffer;
		RendererFrameStatistics* m_Statistics = nullptr;
	};

	template<typename T>
	std::span<const std::byte> AsBytes(const T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>, "Command data must be trivially copyable");
		return std::span<const std::byte>(
			reinterpret_cast<const std::byte*>(&value),
			sizeof(T));
	}
}
