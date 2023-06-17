#include "pch.h"
#include "Image.h"
#include "Renderer/Renderer.h"
#include "Renderer/ResourceManager.h"
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
			case ImageFormat::RGB:			   return VK_FORMAT_R8G8B8_UNORM;
			case ImageFormat::RGBA:            return VK_FORMAT_R8G8B8A8_UNORM;
			case ImageFormat::SRGBA:           return VK_FORMAT_R8G8B8A8_SRGB;
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

		static VkImageViewType VulkanImageViewType(ImageViewType viewType)
		{
			switch (viewType)
			{
			case ImageViewType::Image2D:    return VK_IMAGE_VIEW_TYPE_2D;
			case ImageViewType::ImageCube:	return VK_IMAGE_VIEW_TYPE_CUBE;
			}
			CORE_ASSERT(false, "Unknown view type");
			return (VkImageViewType)0;
		}
	}

	Image::Image(const ImageSpecification& specification)
		: m_Specification(specification), m_CurrentImgLayout(VK_IMAGE_LAYOUT_UNDEFINED)
	{
	}

	Image::~Image()
	{
		if (!m_Image || !m_MemoryAlloc)
			return;

		ResourceManager::SubmitResourceFree([image = m_Image, memoryAlloc = m_MemoryAlloc]()
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			ResourceAllocator allocator("VulkanImage2D");
			allocator.DestroyImage(image, memoryAlloc);
		});

		m_Image = nullptr;
	}


	void Image::Invalidate()
	{
		CORE_ASSERT(m_Specification.Width > 0 && m_Specification.Height > 0, "Width or height less equal than zero!");

		Release();

		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		ResourceAllocator allocator("Image2D");

		// Define the usage type of the image
		// TODO: this (probably) shouldn't be implied
		VkImageUsageFlags usage;
		if (HasFlags(SAMPLE_COMPATIBLE))
		{
			usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		}
		if (m_Specification.Usage == ImageUsage::Attachment)
		{
			if (Image::IsDepthFormat(m_Specification.Format))
				usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			else
				usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		else if (m_Specification.Usage == ImageUsage::Sampler)
		{
			usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}
		else if (m_Specification.Usage == ImageUsage::Storage)
		{
			usage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}

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
		if (HasFlags(CUBE_COMPATIBLE))
		{
			imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		}
		m_MemoryAlloc = allocator.AllocateImage(imageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY, m_Image);
	}

	void Image::Release()
	{
		if (!m_Image || !m_MemoryAlloc)
			return;

		ResourceManager::SubmitResourceFree([image = m_Image, allocation = m_MemoryAlloc]() mutable
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			ResourceAllocator allocator("VulkanImage2D");
			allocator.DestroyImage(image, allocation);
		});

#if OLD_RENDERER
		Renderer::SubmitResourceFree([image = m_Image, allocation = m_MemoryAlloc]() mutable
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			ResourceAllocator allocator("VulkanImage2D");
			allocator.DestroyImage(image, allocation);
		});
#endif
		m_Image = nullptr;
		m_MemoryAlloc = nullptr;
	}

	void Image::SetLayout(VkCommandBuffer cmd, VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask)
	{
		if (newImageLayout == m_CurrentImgLayout)
			return;

		const VkImageAspectFlags aspectMask = Image::IsDepthFormat(m_Specification.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		utils::SetImageLayout(cmd, m_Image,
			aspectMask,
			m_CurrentImgLayout, newImageLayout,
			srcStageMask, dstStageMask);

		m_CurrentImgLayout = newImageLayout;
	}

	void Image::SetLayout(VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask)
	{
		VkCommandBuffer cmdBuffer = Renderer::GetCommandBuffer(true);
		SetLayout(cmdBuffer, newImageLayout, srcStageMask, dstStageMask);
		Renderer::FlushCommandBuffer(cmdBuffer);
	}

	void Image::SetImageLayout(const utils::ImageLayoutSpecification& imgLayoutSpec)
	{
		if (m_CurrentImgLayout == imgLayoutSpec.NewImageLayout)
			return;

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = imgLayoutSpec.AspectMask;
		subresourceRange.baseMipLevel = imgLayoutSpec.BaseMipLevel;
		subresourceRange.levelCount = imgLayoutSpec.LevelCount;
		subresourceRange.layerCount = imgLayoutSpec.LayerCount;

		utils::SetImageLayout(imgLayoutSpec.CommandBuffer, imgLayoutSpec.Image, m_CurrentImgLayout, imgLayoutSpec.NewImageLayout,
			subresourceRange,
			imgLayoutSpec.SrcStageMask, imgLayoutSpec.DstStageMask);

		m_CurrentImgLayout = imgLayoutSpec.NewImageLayout;
	}

	void Image::InsertImageMemoryBarrier(VkCommandBuffer cmdbuffer,
		VkAccessFlags srcAccessMask,
		VkAccessFlags dstAccessMask,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		VkImageSubresourceRange subresourceRange)
	{
		if (m_CurrentImgLayout == newImageLayout)
			return;

		utils::InsertImageMemoryBarrier(cmdbuffer, m_Image, srcAccessMask, dstAccessMask, m_CurrentImgLayout, newImageLayout, srcStageMask, dstStageMask, subresourceRange);
		m_CurrentImgLayout = newImageLayout;
	}

#if 0
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
#endif

	ImageView::ImageView(const ImageViewProperties& imgViewProp)
		: m_Properties(imgViewProp)
	{

	}

	ImageView::~ImageView()
	{
		if (!m_ImageView)
			return;

		ResourceManager::SubmitResourceFree([imageView = m_ImageView, sampler = m_Sampler]()
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			NV_CORE_WARN("Image2D::Release ImageView = {0}", (const void*)imageView);
			vkDestroyImageView(vulkanDevice, imageView, nullptr);

			if (sampler)
				vkDestroySampler(vulkanDevice, sampler, nullptr);
		});
		m_ImageView = nullptr;
		m_Sampler = nullptr;
	}

	void ImageView::UpdateDescriptor()
	{
		Image* image = ResourceManager::GetImage(m_Properties.Image);
		const ImageSpecification& imgSpec = image->GetSpecification();
		// Image Layout when use
		if (imgSpec.Usage == ImageUsage::Storage)
			m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		else if (imgSpec.Format == ImageFormat::DEPTH24STENCIL8 || imgSpec.Format == ImageFormat::DEPTH32F)
			m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		else if(m_Properties.SamplerSpec.ShaderResource)
			m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		else
			m_DescriptorImageInfo.imageLayout = image->GetImageLayout();
		// Image to bind to set
		m_DescriptorImageInfo.imageView = m_ImageView;
		// Sampler to use for set
		m_DescriptorImageInfo.sampler = m_Sampler;
	}

	void ImageView::Release()
	{
		if (!m_ImageView)
			return;

		ResourceManager::SubmitResourceFree([imageView = m_ImageView, sampler = m_Sampler]() mutable
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			NV_CORE_WARN("Image2D::Release ImageView = {0}", (const void*)imageView);
			vkDestroyImageView(vulkanDevice, imageView, nullptr);

			if (sampler)
				vkDestroySampler(vulkanDevice, sampler, nullptr);
		});

#if OLD_RENDERER
		Renderer::SubmitResourceFree([imageView = m_ImageView, sampler = m_Sampler]() mutable
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			NV_CORE_WARN("Image2D::Release ImageView = {0}", (const void*)imageView);
			vkDestroyImageView(vulkanDevice, imageView, nullptr);

			if (sampler)
				vkDestroySampler(vulkanDevice, sampler, nullptr);
		});
#endif
		m_ImageView = nullptr;


	}

	void ImageView::Invalidate()
	{
		Release();
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		Image* image = ResourceManager::GetImage(m_Properties.Image);
		const ImageSpecification& imgSpec = image->GetSpecification();
		VkImage vkImage = image->GetHandle();
		/** /
		if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8)
			aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		/**/
		/////////////////////////////////////////////////
		////// Create Image View
		/////////////////////////////////////////////////
		const VkFormat vulkanFormat = utils::VulkanImageFormat(imgSpec.Format);
		const VkImageAspectFlags aspectMask = Image::IsDepthFormat(imgSpec.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		VkImageViewCreateInfo imageViewCreateInfo = {};
		imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCreateInfo.viewType = utils::VulkanImageViewType(m_Properties.ViewType);
		imageViewCreateInfo.format = vulkanFormat;
		imageViewCreateInfo.flags = 0;
		// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
		// In this case each image view access one layer of the image
		imageViewCreateInfo.subresourceRange = {};
		imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
		imageViewCreateInfo.subresourceRange.baseMipLevel = m_Properties.BaseMip;
		imageViewCreateInfo.subresourceRange.levelCount = m_Properties.MipCount;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = m_Properties.BaseLayer;
		imageViewCreateInfo.subresourceRange.layerCount = m_Properties.LayerCount;
		imageViewCreateInfo.image = vkImage;

		VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_ImageView));

		if (m_Properties.SamplerSpec.ShaderResource)
		{
			const SamplerSpecification& samplerSpec = m_Properties.SamplerSpec;
			////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// CREATE TEXTURE SAMPLER
			////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			VkSamplerCreateInfo samplerCreateInfo{};
			samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerCreateInfo.magFilter = utils::VulkanSamplerFilter(samplerSpec.SamplerFilter);	// How to render when the image is magnified on screen
			samplerCreateInfo.minFilter = utils::VulkanSamplerFilter(samplerSpec.SamplerFilter);	// How to render when the image is minified on screen
			samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerCreateInfo.addressModeU = utils::VulkanSamplerWrap(samplerSpec.SamplerWrap);		// How to handle texture wrap in X (U) direction
			samplerCreateInfo.addressModeV = utils::VulkanSamplerWrap(samplerSpec.SamplerWrap);		// How to handle texture wrap in Y (V) direction
			samplerCreateInfo.addressModeW = utils::VulkanSamplerWrap(samplerSpec.SamplerWrap);		// How to handle texture wrap in Z (W) direction
			samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;													// Whether the coords should be normalize (0 to 1) (VK_FALSE = normalize coords)
			samplerCreateInfo.mipLodBias = 0.0f;
			samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
			samplerCreateInfo.minLod = (float)samplerSpec.MinLOD;
			samplerCreateInfo.maxLod = (float)samplerSpec.MaxLOD;
			samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;		// Border beyond texture (only work for texture clamp)
			// Enable anisotropic filtering
			// This feature is optional, so we must check if it's supported on the device
			if (samplerSpec.Anisotropy)
			{
				samplerCreateInfo.maxAnisotropy = Renderer::GetDeviceLimits().maxSamplerAnisotropy;		// Anisotropy sample level
				samplerCreateInfo.anisotropyEnable = VK_TRUE;
			}
			else
			{
				samplerCreateInfo.maxAnisotropy = 1.f;		// Anisotropy sample level
				samplerCreateInfo.anisotropyEnable = VK_FALSE;
			}

			VK_CHECK_RESULT(vkCreateSampler(device, &samplerCreateInfo, nullptr, &m_Sampler));

			// Update Descriptor
			UpdateDescriptor();
		}

		// TODO: Move to TextureStorage class
#if OLD_RENDERER
		if (m_Properties.Usage == ImageUsage::Storage)
		{
			// Transition image to GENERAL layout
			VkCommandBuffer commandBuffer = Renderer::GetCommandBuffer(true);

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = m_Properties.MipCount;
			subresourceRange.layerCount = m_Properties.LayerCount;

			utils::InsertImageMemoryBarrier(commandBuffer, vkImage,
				0, 0,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				subresourceRange);

			Renderer::FlushCommandBuffer(commandBuffer);
		}
#endif
	}
}