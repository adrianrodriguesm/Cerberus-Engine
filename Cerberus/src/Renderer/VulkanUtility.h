#pragma once

namespace cerberus::utils
{
	struct ImageLayoutSpecification
	{
		VkCommandBuffer CommandBuffer;
		VkImage Image;
		VkImageAspectFlags AspectMask;
		VkImageLayout OldImageLayout, NewImageLayout;
		VkPipelineStageFlags SrcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkPipelineStageFlags DstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		uint32_t BaseMipLevel = 0;
		uint32_t LevelCount = 1;
		uint32_t LayerCount = 6;

	};
	void InsertImageMemoryBarrier(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkAccessFlags srcAccessMask,
		VkAccessFlags dstAccessMask,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		VkImageSubresourceRange subresourceRange);

	// Create an image memory barrier for changing the layout of
	// an image and put it into an active command buffer
	void SetImageLayout(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkImageSubresourceRange subresourceRange,
		VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

	// Fixed sub resource on first mip level and layer
	// VkImageAspectFlags = Aspect of image being altered
	void SetImageLayout(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkImageAspectFlags aspectMask,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

	void SetImageLayout(const ImageLayoutSpecification& imgLayoutSpec);
}