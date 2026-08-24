#pragma once

#include "Renderer/RHI/Command/CommandList.h"

namespace cerberus
{
	// Typed recording view for copies, blits and image transitions. Queue
	// allocation/submission remains owned by CommandBuffer or
	// TransientCommandExecutor.
	class TransferCommandList final : public CommandListBase
	{
	public:
		explicit TransferCommandList(CommandBufferView commandBuffer);

		void CopyBuffer(
			VkBuffer source,
			VkBuffer destination,
			VkDeviceSize size,
			VkDeviceSize sourceOffset = 0,
			VkDeviceSize destinationOffset = 0) const;
		void CopyBuffer(
			VkBuffer source,
			VkBuffer destination,
			std::span<const VkBufferCopy> regions) const;

		void CopyBufferToImage(
			VkBuffer source,
			VkImage destination,
			VkImageLayout destinationLayout,
			std::span<const VkBufferImageCopy> regions) const;

		void BlitImage(
			VkImage source,
			VkImageLayout sourceLayout,
			VkImage destination,
			VkImageLayout destinationLayout,
			std::span<const VkImageBlit> regions,
			VkFilter filter) const;

		void TransitionImageLayout(
			VkImage image,
			VkImageLayout oldLayout,
			VkImageLayout newLayout,
			VkImageSubresourceRange subresourceRange,
			VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) const;

		void ImageBarrier(
			VkImage image,
			VkAccessFlags sourceAccess,
			VkAccessFlags destinationAccess,
			VkImageLayout oldLayout,
			VkImageLayout newLayout,
			VkPipelineStageFlags sourceStage,
			VkPipelineStageFlags destinationStage,
			VkImageSubresourceRange subresourceRange) const;
	};
}
