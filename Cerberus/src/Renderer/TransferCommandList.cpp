#include "pch.h"
#include "TransferCommandList.h"

namespace cerberus
{
	namespace
	{
		VkAccessFlags SourceAccessForLayout(VkImageLayout layout)
		{
			switch (layout)
			{
			case VK_IMAGE_LAYOUT_UNDEFINED: return 0;
			case VK_IMAGE_LAYOUT_PREINITIALIZED: return VK_ACCESS_HOST_WRITE_BIT;
			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return VK_ACCESS_TRANSFER_READ_BIT;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return VK_ACCESS_TRANSFER_WRITE_BIT;
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_ACCESS_SHADER_READ_BIT;
			case VK_IMAGE_LAYOUT_GENERAL: return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			default: return 0;
			}
		}

		VkAccessFlags DestinationAccessForLayout(VkImageLayout layout)
		{
			switch (layout)
			{
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return VK_ACCESS_TRANSFER_WRITE_BIT;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return VK_ACCESS_TRANSFER_READ_BIT;
			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_ACCESS_SHADER_READ_BIT;
			case VK_IMAGE_LAYOUT_GENERAL: return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			default: return 0;
			}
		}
	}

	TransferCommandList::TransferCommandList(CommandBufferView commandBuffer)
		: CommandListBase(commandBuffer)
	{
	}

	void TransferCommandList::CopyBuffer(
		VkBuffer source,
		VkBuffer destination,
		VkDeviceSize size,
		VkDeviceSize sourceOffset,
		VkDeviceSize destinationOffset) const
	{
		CORE_ASSERT(size > 0, "CopyBuffer size must be greater than zero");

		VkBufferCopy region{};
		region.srcOffset = sourceOffset;
		region.dstOffset = destinationOffset;
		region.size = size;
		CopyBuffer(source, destination, std::span<const VkBufferCopy>(&region, 1));
	}

	void TransferCommandList::CopyBuffer(
		VkBuffer source,
		VkBuffer destination,
		std::span<const VkBufferCopy> regions) const
	{
		CORE_ASSERT(source != VK_NULL_HANDLE, "CopyBuffer requires a valid source buffer");
		CORE_ASSERT(destination != VK_NULL_HANDLE, "CopyBuffer requires a valid destination buffer");
		if (regions.empty())
			return;

		vkCmdCopyBuffer(
			GetNative(),
			source,
			destination,
			static_cast<uint32_t>(regions.size()),
			regions.data());
	}

	void TransferCommandList::CopyBufferToImage(
		VkBuffer source,
		VkImage destination,
		VkImageLayout destinationLayout,
		std::span<const VkBufferImageCopy> regions) const
	{
		CORE_ASSERT(source != VK_NULL_HANDLE, "CopyBufferToImage requires a valid source buffer");
		CORE_ASSERT(destination != VK_NULL_HANDLE, "CopyBufferToImage requires a valid destination image");
		if (regions.empty())
			return;

		vkCmdCopyBufferToImage(
			GetNative(),
			source,
			destination,
			destinationLayout,
			static_cast<uint32_t>(regions.size()),
			regions.data());
	}

	void TransferCommandList::BlitImage(
		VkImage source,
		VkImageLayout sourceLayout,
		VkImage destination,
		VkImageLayout destinationLayout,
		std::span<const VkImageBlit> regions,
		VkFilter filter) const
	{
		CORE_ASSERT(source != VK_NULL_HANDLE, "BlitImage requires a valid source image");
		CORE_ASSERT(destination != VK_NULL_HANDLE, "BlitImage requires a valid destination image");
		if (regions.empty())
			return;

		vkCmdBlitImage(
			GetNative(),
			source,
			sourceLayout,
			destination,
			destinationLayout,
			static_cast<uint32_t>(regions.size()),
			regions.data(),
			filter);
	}

	void TransferCommandList::TransitionImageLayout(
		VkImage image,
		VkImageLayout oldLayout,
		VkImageLayout newLayout,
		VkImageSubresourceRange subresourceRange,
		VkPipelineStageFlags sourceStage,
		VkPipelineStageFlags destinationStage) const
	{
		CORE_ASSERT(image != VK_NULL_HANDLE, "TransitionImageLayout requires a valid image");
		ImageBarrier(
			image,
			SourceAccessForLayout(oldLayout),
			DestinationAccessForLayout(newLayout),
			oldLayout,
			newLayout,
			sourceStage,
			destinationStage,
			subresourceRange);
	}

	void TransferCommandList::ImageBarrier(
		VkImage image,
		VkAccessFlags sourceAccess,
		VkAccessFlags destinationAccess,
		VkImageLayout oldLayout,
		VkImageLayout newLayout,
		VkPipelineStageFlags sourceStage,
		VkPipelineStageFlags destinationStage,
		VkImageSubresourceRange subresourceRange) const
	{
		CORE_ASSERT(image != VK_NULL_HANDLE, "ImageBarrier requires a valid image");

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.srcAccessMask = sourceAccess;
		barrier.dstAccessMask = destinationAccess;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.image = image;
		barrier.subresourceRange = subresourceRange;

		PipelineBarrier(sourceStage, destinationStage, 0, {}, {},
			std::span<const VkImageMemoryBarrier>(&barrier, 1));
	}
}
