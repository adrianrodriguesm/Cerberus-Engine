#include "pch.h"
#include "Image.h"
#include "Renderer/Core/Renderer.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Renderer/Memory/DeferredResourceDeletionQueue.h"
#include "Renderer/RHI/Command/TransferCommandList.h"

namespace cerberus
{
	namespace utils
	{
		VkFormat VulkanImageFormat(ImageFormat format)
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

	Image::Image(Image&& other) noexcept
		: m_Specification(std::move(other.m_Specification))
		, m_Image(other.m_Image)
		, m_MemoryAlloc(other.m_MemoryAlloc)
		, m_CurrentImgLayout(other.m_CurrentImgLayout)
	{
		other.m_Image = VK_NULL_HANDLE;
		other.m_MemoryAlloc = VK_NULL_HANDLE;
	}

	Image::~Image()
	{
		if (!m_Image || !m_MemoryAlloc)
			return;

		DeferredResourceDeletionQueue::SubmitCurrentFrame([image = m_Image, memoryAlloc = m_MemoryAlloc]()
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			ResourceAllocator allocator("VulkanImage2D");
			allocator.DestroyImage(image, memoryAlloc);
		});

		m_Image = VK_NULL_HANDLE;
		m_MemoryAlloc = VK_NULL_HANDLE;
	}


	void Image::Invalidate()
	{
		CORE_ASSERT(m_Specification.Width > 0 && m_Specification.Height > 0, "Width or height less equal than zero!");

		Release();
		m_CurrentImgLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		ResourceAllocator allocator("Image2D");

		// Define the usage type of the image
		// TODO: this (probably) shouldn't be implied
		VkImageUsageFlags usage = 0;
		if (HasFlags(SAMPLE_COMPATIBLE))
		{
			usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
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
			// Storage images used by environment preprocessing are also copied/blitted
			// while their mip chain is built.
			usage |= VK_IMAGE_USAGE_STORAGE_BIT |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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

		DeferredResourceDeletionQueue::SubmitCurrentFrame([image = m_Image, allocation = m_MemoryAlloc]() mutable
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			ResourceAllocator allocator("VulkanImage2D");
			allocator.DestroyImage(image, allocation);
		});
		m_Image = nullptr;
		m_MemoryAlloc = nullptr;
		m_CurrentImgLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	void Image::SetLayout(
		const TransferCommandList& commandList,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask)
	{
		const VkImageAspectFlags aspectMask = Image::IsDepthFormat(m_Specification.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = aspectMask;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.layerCount = 1;
		SetLayout(commandList, newImageLayout, srcStageMask, dstStageMask, subresourceRange);
	}

	void Image::SetLayout(
		const TransferCommandList& commandList,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		VkImageSubresourceRange subresourceRange)
	{
		if (newImageLayout == m_CurrentImgLayout)
			return;

		commandList.TransitionImageLayout(
			m_Image,
			m_CurrentImgLayout,
			newImageLayout,
			subresourceRange,
			srcStageMask,
			dstStageMask);

		m_CurrentImgLayout = newImageLayout;
	}

	void Image::SetLayout(VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask)
	{
		TransientCommandBuffer cmdBuffer = Renderer::GetTransientCommandExecutor().BeginGraphics();
		TransferCommandList transfer(cmdBuffer.GetRecordingView());
		SetLayout(transfer, newImageLayout, srcStageMask, dstStageMask);
		cmdBuffer.SubmitAndWait();
	}

	void Image::InsertImageMemoryBarrier(const CommandListBase& commandList,
		VkAccessFlags srcAccessMask,
		VkAccessFlags dstAccessMask,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		VkImageSubresourceRange subresourceRange)
	{
		InsertImageMemoryBarrier(
			commandList,
			srcAccessMask,
			dstAccessMask,
			m_CurrentImgLayout,
			newImageLayout,
			srcStageMask,
			dstStageMask,
			subresourceRange);
	}

	void Image::InsertImageMemoryBarrier(const CommandListBase& commandList,
		VkAccessFlags srcAccessMask,
		VkAccessFlags dstAccessMask,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		VkImageSubresourceRange subresourceRange)
	{
		// A memory dependency can be required even when the image remains in the
		// same layout (for example, a storage-image write in one path-tracing
		// dispatch followed by a read/write in the next dispatch).
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.srcAccessMask = srcAccessMask;
		barrier.dstAccessMask = dstAccessMask;
		barrier.oldLayout = oldImageLayout;
		barrier.newLayout = newImageLayout;
		barrier.image = m_Image;
		barrier.subresourceRange = subresourceRange;

		commandList.PipelineBarrier(
			srcStageMask,
			dstStageMask,
			0,
			{},
			{},
			std::span<const VkImageMemoryBarrier>(&barrier, 1));
		m_CurrentImgLayout = newImageLayout;
	}

	ImageView::ImageView(const ImageViewProperties& imgViewProp)
		: m_Properties(imgViewProp)
	{

	}

	ImageView::ImageView(ImageView&& other) noexcept
		: m_Properties(std::move(other.m_Properties))
		, m_ImageView(other.m_ImageView)
		, m_Sampler(other.m_Sampler)
		, m_DescriptorImageInfo(other.m_DescriptorImageInfo)
	{
		other.m_ImageView = VK_NULL_HANDLE;
		other.m_Sampler = VK_NULL_HANDLE;
	}

	ImageView::~ImageView()
	{
		if (!m_ImageView)
			return;

		DeferredResourceDeletionQueue::SubmitCurrentFrame([imageView = m_ImageView, sampler = m_Sampler]()
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			NV_CORE_WARN("Image2D::Release ImageView = {0}", (const void*)imageView);
			vkDestroyImageView(vulkanDevice, imageView, nullptr);

			if (sampler)
				vkDestroySampler(vulkanDevice, sampler, nullptr);
		});
		m_ImageView = VK_NULL_HANDLE;
		m_Sampler = VK_NULL_HANDLE;
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

		DeferredResourceDeletionQueue::SubmitCurrentFrame([imageView = m_ImageView, sampler = m_Sampler]() mutable
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			NV_CORE_WARN("Image2D::Release ImageView = {0}", (const void*)imageView);
			vkDestroyImageView(vulkanDevice, imageView, nullptr);

			if (sampler)
				vkDestroySampler(vulkanDevice, sampler, nullptr);
		});
		m_ImageView = nullptr;


	}

	void ImageView::Invalidate()
	{
		Release();
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		Image* image = ResourceManager::GetImage(m_Properties.Image);
		const ImageSpecification& imgSpec = image->GetSpecification();
		VkImage vkImage = image->GetHandle();
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
	}
}
