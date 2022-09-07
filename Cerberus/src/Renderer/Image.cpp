#include "pch.h"
#include "Image.h"
#include "Renderer/Renderer.h"
#include "Renderer/VulkanUtility.h"
namespace cerberus
{
	namespace utils 
	{

		inline VkFormat VulkanImageFormat(ImageFormat format)
		{
			switch (format)
			{
			case ImageFormat::RED32F:          return VK_FORMAT_R32_SFLOAT;
			case ImageFormat::RG16F:		   return VK_FORMAT_R16G16_SFLOAT;
			case ImageFormat::RG32F:		   return VK_FORMAT_R32G32_SFLOAT;
			case ImageFormat::RGBA:            return VK_FORMAT_R8G8B8A8_UNORM;
			case ImageFormat::SRGBA:            return VK_FORMAT_R8G8B8A8_SRGB;
			case ImageFormat::RGBA16F:         return VK_FORMAT_R16G16B16A16_SFLOAT;
			case ImageFormat::RGBA32F:         return VK_FORMAT_R32G32B32A32_SFLOAT;
			case ImageFormat::DEPTH32F:        return VK_FORMAT_D32_SFLOAT;
			case ImageFormat::DEPTH24STENCIL8: return RendererContext::GetCurrentDevice()->GetPhysicalDevice()->GetDepthFormat();
			}
			CORE_ASSERT(false, "Image format does not exits!");
			return VK_FORMAT_UNDEFINED;
		}

	
		static VkSamplerAddressMode VulkanSamplerWrap(SamplerWrap wrap)
		{
			switch (wrap)
			{
			case SamplerWrap::Clamp:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case SamplerWrap::Repeat:  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
			}
			CORE_ASSERT(false, "Unknown wrap mode");
			return (VkSamplerAddressMode)0;
		}

		static VkFilter VulkanSamplerFilter(SamplerFilter filter)
		{
			switch (filter)
			{
			case SamplerFilter::Linear:   return VK_FILTER_LINEAR;
			case SamplerFilter::Nearest:  return VK_FILTER_NEAREST;
			}
			CORE_ASSERT(false, "Unknown filter");
			return (VkFilter)0;
		}
	}

	Image2D::Image2D(const ImageSpecification& specification)
		: m_Specification(specification)
	{
		
	}
	Image2D::~Image2D()
	{
		Release();
	}

	void Image2D::Invalidate()
	{
		CORE_ASSERT(m_Specification.Width > 0 && m_Specification.Height > 0, "Width or height less equal than zero!");

		Release();

		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		VulkanAllocator allocator("Image2D");


		// Define the usage type of the image
		// TODO: this (probably) shouldn't be implied
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT; 
		if (m_Specification.Usage == ImageUsage::Attachment)
		{
			if (Image::IsDepthFormat(m_Specification.Format))
				usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			else
				usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		else if (m_Specification.Usage == ImageUsage::Texture)
		{
			usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}
		else if (m_Specification.Usage == ImageUsage::Storage)
		{
			usage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}

		VkImageAspectFlags aspectMask = Image::IsDepthFormat(m_Specification.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		/** /
		if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8)
			aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		/**/
		VkFormat vulkanFormat = utils::VulkanImageFormat(m_Specification.Format);
		/////////////////////////////////////////////////
		////// Create Image
		/////////////////////////////////////////////////
		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = vulkanFormat;
		imageCreateInfo.extent.width = m_Specification.Width;
		imageCreateInfo.extent.height = m_Specification.Height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = m_Specification.Mips;
		imageCreateInfo.arrayLayers = m_Specification.Layers;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL; // Texels are laid out in an implementation-dependent arrangement, for more efficient memory access
		imageCreateInfo.usage = usage;
		m_Info.MemoryAlloc = allocator.AllocateImage(imageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY, m_Info.Image);
		/////////////////////////////////////////////////
		////// Create Image View
		/////////////////////////////////////////////////
		VkImageViewCreateInfo imageViewCreateInfo = {};
		imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCreateInfo.viewType = m_Specification.Layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.format = vulkanFormat;
		imageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		imageViewCreateInfo.flags = 0;
		// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
		// It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
		imageViewCreateInfo.subresourceRange = {};
		imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.levelCount = m_Specification.Mips;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.layerCount = m_Specification.Layers;
		imageViewCreateInfo.image = m_Info.Image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_Info.ImageView));

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// CREATE TEXTURE SAMPLER
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		VkSamplerCreateInfo samplerCreateInfo{};
		samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCreateInfo.magFilter = utils::VulkanSamplerFilter(m_Specification.SamplerSpec.SamplerFilter);	// How to render when the image is magnified on screen
		samplerCreateInfo.minFilter = utils::VulkanSamplerFilter(m_Specification.SamplerSpec.SamplerFilter);	// How to render when the image is minified on screen
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.addressModeU = utils::VulkanSamplerWrap(m_Specification.SamplerSpec.SamplerWrap);		// How to handle texture wrap in X (U) direction
		samplerCreateInfo.addressModeV = utils::VulkanSamplerWrap(m_Specification.SamplerSpec.SamplerWrap);		// How to handle texture wrap in Y (V) direction
		samplerCreateInfo.addressModeW = utils::VulkanSamplerWrap(m_Specification.SamplerSpec.SamplerWrap);		// How to handle texture wrap in Z (W) direction
		samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;													// Whether the coords should be normalize (0 to 1) (VK_FALSE = normalize coords)
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerCreateInfo.minLod = 0.0f;
		samplerCreateInfo.maxLod = (float)m_Specification.Mips;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;		// Border beyond texture (only work for texture clamp)
		// Enable anisotropic filtering
		// This feature is optional, so we must check if it's supported on the device
		if (m_Specification.SamplerSpec.Anisotropy)
		{
			samplerCreateInfo.maxAnisotropy = Renderer::GetDeviceLimits().maxSamplerAnisotropy;		// Anisotropy sample level
			samplerCreateInfo.anisotropyEnable = VK_TRUE;
		}
		else
		{
			samplerCreateInfo.maxAnisotropy = 1.f;		// Anisotropy sample level
			samplerCreateInfo.anisotropyEnable = VK_FALSE;
		}

		VK_CHECK_RESULT(vkCreateSampler(device, &samplerCreateInfo, nullptr, &m_Info.Sampler));

		if (m_Specification.Usage == ImageUsage::Storage)
		{
			// Transition image to GENERAL layout
			VkCommandBuffer commandBuffer = Renderer::GetCommandBuffer(true);

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = m_Specification.Mips;
			subresourceRange.layerCount = m_Specification.Layers;

			utils::InsertImageMemoryBarrier(commandBuffer, m_Info.Image,
				0, 0,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				subresourceRange);

			Renderer::FlushCommandBuffer(commandBuffer);
		}

		// Update Descriptor
		UpdateDescriptor();
	}
	void Image2D::Release()
	{
		if (!m_Info.Image)
			return;

		
		Renderer::SubmitResourceFree([info = m_Info, perLayerImageViews = m_PerLayerImageViews]() mutable
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			for (auto& imageViewLayer : perLayerImageViews)
			{
				NV_CORE_WARN("Image2D::Release ImageView Layer = {0}", (const void*)info.ImageView);
				vkDestroyImageView(vulkanDevice, imageViewLayer, nullptr);
			}
			perLayerImageViews.clear();
			NV_CORE_WARN("Image2D::Release ImageView = {0}", (const void*)info.ImageView);
			vkDestroyImageView(vulkanDevice, info.ImageView, nullptr);
			vkDestroySampler(vulkanDevice, info.Sampler, nullptr);
			VulkanAllocator allocator("VulkanImage2D");
			allocator.DestroyImage(info.Image, info.MemoryAlloc);
		});
		m_PerLayerImageViews.clear();
		m_Info.Image = nullptr;
		m_Info.ImageView = nullptr;
		m_Info.Sampler = nullptr;

	}
	void Image2D::UpdateDescriptor()
	{
		// Image Layout when use
		if (m_Specification.Usage == ImageUsage::Storage)
			m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		else if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8 || m_Specification.Format == ImageFormat::DEPTH32F)
			m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		else
			m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// Image to bind to set
		m_DescriptorImageInfo.imageView = m_Info.ImageView;
		// Sampler to use for set
		m_DescriptorImageInfo.sampler = m_Info.Sampler;
	}

	void Image2D::CreatePerLayerImageViews()
	{
		CORE_ASSERT(m_Specification.Layers > 1, "");
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		VkImageAspectFlags aspectMask = Image::IsDepthFormat(m_Specification.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		/** /
		if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8)
			aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		/**/
		const VkFormat vulkanFormat = utils::VulkanImageFormat(m_Specification.Format);
		m_PerLayerImageViews.resize(m_Specification.Layers);
		for (uint32_t layer = 0; layer < m_Specification.Layers; layer++)
		{
			VkImageViewCreateInfo imageViewCreateInfo = {};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewCreateInfo.format = vulkanFormat;
			imageViewCreateInfo.flags = 0;
			// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
			// In this case each image view access one layer of the image
			imageViewCreateInfo.subresourceRange = {};
			imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
			imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
			imageViewCreateInfo.subresourceRange.levelCount = m_Specification.Mips;
			imageViewCreateInfo.subresourceRange.baseArrayLayer = layer;
			imageViewCreateInfo.subresourceRange.layerCount = 1;
			imageViewCreateInfo.image = m_Info.Image;
			VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_PerLayerImageViews[layer]));
		}

	}

	void Image2D::CreatePerSpecificLayerImageViews(const std::vector<uint32_t>& layerIndices)
	{
		CORE_ASSERT(m_Specification.Layers > 1, "");
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		VkImageAspectFlags aspectMask = Image::IsDepthFormat(m_Specification.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8)
			aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		
		const VkFormat vulkanFormat = utils::VulkanImageFormat(m_Specification.Format);

		if (m_PerLayerImageViews.empty())
			m_PerLayerImageViews.resize(m_Specification.Layers);

		for (uint32_t layer : layerIndices)
		{
			VkImageViewCreateInfo imageViewCreateInfo = {};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewCreateInfo.format = vulkanFormat;
			imageViewCreateInfo.flags = 0;
			// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
			// In this case each image view access one layer of the image
			imageViewCreateInfo.subresourceRange = {};
			imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
			imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
			imageViewCreateInfo.subresourceRange.levelCount = m_Specification.Mips;
			imageViewCreateInfo.subresourceRange.baseArrayLayer = layer;
			imageViewCreateInfo.subresourceRange.layerCount = 1;
			imageViewCreateInfo.image = m_Info.Image;
			VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_PerLayerImageViews[layer]));
		}
	}
}