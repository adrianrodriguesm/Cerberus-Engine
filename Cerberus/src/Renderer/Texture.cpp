#include "pch.h"
#include "Texture.h"
#include "stb_image.h"
#include "Renderer/Renderer.h"
#include "Renderer/ComputePipeline.h"
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


	Texture2D::Texture2D(ImageFormat format, const uint32_t width, const uint32_t height, const void* data, TextureProperties properties)
		: m_Properties(properties)
	{
		uint32_t imageSize = Image::GetImageMemorySize(format, width, height);
		if (imageSize)
			m_ImageData = Buffer::Copy(data, imageSize);

		// Check if the image was succesfully loaded
		if (!m_ImageData.Data)
		{
			CORE_ASSERT(false, "Falied to load image!");
			return;
		}

		ImageSpecification imageSpec;
		imageSpec.DebugName = "Texture2D";
		imageSpec.Usage = ImageUsage::Sampler;
		imageSpec.Format = format;
		imageSpec.Width = width;
		imageSpec.Height = height;
		imageSpec.Layers = 1;
		imageSpec.Mips = m_Properties.GenerateMips ? Image::CalculateMipCount(width, height) : 1;
		imageSpec.Flags = SAMPLE_COMPATIBLE;
		m_Image = ResourceManager::CreateImage(imageSpec);

		ImageViewProperties viewProp;
		viewProp.ViewType = ImageViewType::Image2D;
		viewProp.BaseLayer = 0;
		viewProp.LayerCount = 1;
		viewProp.BaseMip = 0;
		viewProp.MipCount = m_Properties.GenerateMips ? GetMipLevelCount() : 1;
		viewProp.Image = m_Image;
		m_ImageView = ResourceManager::CreateImageView(viewProp);
		Invalidate(format, width, height);
/** /
		Texture2D* instance = this;
		Renderer::Submit([instance]()
		{
			instance->Invalidate();
		});
/**/
	}
#if 0
	Texture2D::Texture2D(const Ref<Image2D>& image2D)
		: m_Image(image2D), m_Format(image2D->GetSpecification().Format), m_Width(image2D->GetSpecification().Width), 
		m_Height(image2D->GetSpecification().Height)
	{
	}
#endif
	Texture2D::~Texture2D()
	{
		ResourceManager::DestroyImageView(m_ImageView);
		ResourceManager::DestroyImage(m_Image);

		if (m_ImageData)
			m_ImageData.Release();
	}


	ImageFormat Texture2D::GetFormat() const
	{
		Image* img = ResourceManager::GetImage(m_Image);
		if (!img)
		{
			CORE_ASSERT(false, "Invalid image");
			return ImageFormat();
		}

		return img->GetSpecification().Format;
		
	}

	uint32_t Texture2D::GetWidth() const
	{
		Image* img = ResourceManager::GetImage(m_Image);
		if (!img)
		{
			CORE_ASSERT(false, "Invalid image");
			return 0;
		}

		return img->GetWidth();
	}

	uint32_t Texture2D::GetHeight() const
	{
		Image* img = ResourceManager::GetImage(m_Image);
		if (!img)
		{
			CORE_ASSERT(false, "Invalid image");
			return 0;
		}

		return img->GetHeight();
	}

	uint64_t Texture2D::GetHash() const
	{
		ImageView* imgView = ResourceManager::GetImageView(m_ImageView);
		if (!imgView)
		{
			CORE_ASSERT(false, "Invalid image view");
			return 0;
		}

		return (uint64_t)imgView->GetHandle();
	}

	void Texture2D::Invalidate(ImageFormat format, const uint32_t width, const uint32_t height)
	{
		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		ImageView* imgView = ResourceManager::GetImageView(m_ImageView);
		ImageViewProperties& viewProp = imgView->GetProperties();

		// Invalidate Image
		Image* image = ResourceManager::GetImage(m_Image);
		ImageSpecification& imageSpec = image->GetSpecification();
		imageSpec.Format = format;
		imageSpec.Width = width;
		imageSpec.Height = height;
		imageSpec.Mips = m_Properties.GenerateMips ? GetMipLevelCount() : 1;
		image->Invalidate();
		// Invalidate ImageView
		SamplerSpecification samplerSpec = { m_Properties.SamplerWrap, m_Properties.SamplerFilter };
		samplerSpec.ShaderResource = true;
		viewProp.Image = m_Image;
		viewProp.SamplerSpec = samplerSpec;
		imgView->Invalidate();

		VkImage vkImage = image->GetHandle();
		VkImageView vkImageView = imgView->GetHandle();

		VkDeviceSize size = (uint64_t)m_ImageData.Size;
		uint32_t mipCount = m_Properties.GenerateMips ? GetMipLevelCount() : 1;
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// STAGING BUFFER
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		ResourceAllocator allocator("Texture2D");
		// Create staging buffer to hold loaded data, ready to copy to device
		VkBuffer stagingBuffer;
		VkBufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = size;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		// Allocate and Create Staging Buffer
		VmaAllocation stagingBufferAllocation = allocator.AllocateBuffer(bufferCreateInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer);
		// Copy data to staging buffer
		uint8_t* destData = allocator.MapMemory<uint8_t>(stagingBufferAllocation);
		CORE_ASSERT(m_ImageData.Data, "Texture does not hold any data!");
		memcpy(destData, m_ImageData.Data, size);
		allocator.UnmapMemory(stagingBufferAllocation);

		// Get Copy/Transfer Command Buffer
		VkCommandBuffer copyCommandBuffer = Renderer::GetCommandBuffer(true);

		// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition 
		// Source pipeline stage is host write/read exection (VK_PIPELINE_STAGE_HOST_BIT)
		// Destination pipeline stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
		image->SetLayout(copyCommandBuffer,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
							VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		// Define Region of the Image to Copy 
		VkBufferImageCopy bufferCopyRegion = {};
		bufferCopyRegion.bufferOffset = 0;											// Offset into data
		bufferCopyRegion.bufferRowLength = 0;										// Row length of data to calculate data spacing
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;	// Which aspect of image to copy
		bufferCopyRegion.imageSubresource.mipLevel = 0;								// Mipmap level to copy
		bufferCopyRegion.imageSubresource.baseArrayLayer = 0;						// Starting array layer (if array)
		bufferCopyRegion.imageSubresource.layerCount = 1;							// Number of layers to copy starting at baseArrayLayer
		bufferCopyRegion.imageExtent = { width, height, 1 };						// Size of region to copy as (x, y, z) values

		// Copy buffer to given image
		vkCmdCopyBufferToImage(copyCommandBuffer, stagingBuffer, vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
		
		VkImageLayout dstLayout;
		VkPipelineStageFlags dstPipelineStgFlags;
		if (m_Properties.GenerateMips)
		{
			dstLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			dstPipelineStgFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else
		{
			dstLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			dstPipelineStgFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}

		// Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
		image->SetLayout(copyCommandBuffer,dstLayout,
			VK_PIPELINE_STAGE_TRANSFER_BIT, dstPipelineStgFlags);


		// Flush Copy/Transfer Command Buffer (not longer needed)
		Renderer::FlushCommandBuffer(copyCommandBuffer);
		allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);

		if (m_Properties.GenerateMips)
			GenerateMips();
	}

	uint32_t Texture2D::GetMipLevelCount() const
	{
		Image* img = ResourceManager::GetImage(m_Image);
		return Image::CalculateMipCount(img->GetWidth(), img->GetHeight());
	}

	std::pair<uint32_t, uint32_t> Texture2D::GetMipSize(uint32_t mip) const
	{
		Image* img = ResourceManager::GetImage(m_Image);
		return std::pair<uint32_t, uint32_t>(uint32_t(img->GetWidth() >> mip), uint32_t(img->GetHeight() >> mip));
	}

	void Texture2D::GenerateMips()
	{
		uint32_t mipLevels = GetMipLevelCount();

		Image* image = ResourceManager::GetImage(m_Image);
		VkImage vkImage = image->GetHandle();
		uint32_t width = image->GetWidth();
		uint32_t height = image->GetHeight();
		// Get a command buffer to blit the image between the mip levels
		const VkCommandBuffer blitCmd = Renderer::GetCommandBuffer(true);

		for (uint32_t i = 1; i < mipLevels; i++)
		{
			VkImageBlit imageBlit = {};

			// Source
			imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlit.srcSubresource.layerCount = 1;
			imageBlit.srcSubresource.mipLevel = i - 1;
			// The region that data will be blitted from
			imageBlit.srcOffsets[0] = { 0, 0, 0 };
			imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1)); // Note: >> i <=> /2^i
			imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
			imageBlit.srcOffsets[1].z = 1; 	// 1 Since is a 2D image has depth of 1

			// Destination
			imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlit.dstSubresource.layerCount = 1;
			imageBlit.dstSubresource.mipLevel = i;
			// The region that data will be blitted to
			imageBlit.dstOffsets[0] = { 0, 0, 0 };
			imageBlit.dstOffsets[1].x = int32_t(width >> i); // Note: >> i <=> /2^i
			imageBlit.dstOffsets[1].y = int32_t(height >> i);
			imageBlit.dstOffsets[1].z = 1;	// 1 Since is a 2D image has depth of 1

			VkImageSubresourceRange mipSubRange = {};
			mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			mipSubRange.baseMipLevel = i;
			mipSubRange.levelCount = 1;
			mipSubRange.layerCount = 1;

			// Prepare current mip level as image blit destination
			utils::InsertImageMemoryBarrier(blitCmd, vkImage,
				0, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				mipSubRange);


			/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Blit is transfer operation that allows to copying, scaling, and filtering. This operation depends
			// on the layout of the image it operates on. We could transition the entire image to VK_IMAGE_LAYOUT_GENERAL
			// but this will most likely be slow. For optimal performance, the source image should be in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
		    // and the destination image should be in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
			/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			
			// Blit from previous level. Note that info.Image is used for both the srcImage and dstImage  
			// parameter. This is because we're blitting between different levels of the same image.
			vkCmdBlitImage(
				blitCmd,
				vkImage,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				vkImage,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageBlit,
				VK_FILTER_LINEAR); //  We use the VK_FILTER_LINEAR to enable interpolation

			// Prepare current mip level as image blit source for next level
			utils::InsertImageMemoryBarrier(blitCmd, vkImage,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				mipSubRange);
		}

		// After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to SHADER_READ
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.layerCount = 1;
		subresourceRange.levelCount = mipLevels;

		utils::InsertImageMemoryBarrier(blitCmd, vkImage,
			VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, 
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			subresourceRange);

		Renderer::FlushCommandBuffer(blitCmd);

	}

	TextureCube::TextureCube(ImageFormat format, uint32_t width, uint32_t height, TextureProperties properties, const void* data)
		: m_Format(format), m_Width(width), m_Height(height), m_Properties(properties)
	{
		if (data)
		{
			uint32_t imageSize = Image::GetImageMemorySize(m_Format, m_Width, m_Height) * 6; // six layers
			m_ImageData = Buffer::Copy(data, imageSize);
		}

		if (m_Properties.GenerateMips)
		{
			m_MipsLevels = Image::CalculateMipCount(m_Width, m_Height);
		}
		else
		{
			m_MipsLevels = 1;
		}

		ImageSpecification imageSpec;
		imageSpec.DebugName = "TextureCube";
		imageSpec.Format = format;
		imageSpec.Usage = ImageUsage::Sampler;
		imageSpec.Width = width;
		imageSpec.Height = height;
		imageSpec.Layers = 6;
		imageSpec.Mips = m_Properties.GenerateMips ? GetMipLevelCount() : 1;
		imageSpec.Flags = SAMPLE_COMPATIBLE | CUBE_COMPATIBLE;
		m_Image = ResourceManager::CreateImage(imageSpec);

		ImageViewProperties viewProp;
		viewProp.ViewType = ImageViewType::ImageCube;
		viewProp.BaseLayer = 0;
		viewProp.LayerCount = 6;
		viewProp.BaseMip = 0;
		viewProp.MipCount = m_MipsLevels;
		viewProp.Image = m_Image;

		SamplerSpecification SamplerSpec;
		SamplerSpec.SamplerWrap = SamplerWrap::Clamp;
		SamplerSpec.SamplerFilter = SamplerFilter::Linear;
		SamplerSpec.Anisotropy = false;
		SamplerSpec.MinLOD = 0.f;
		SamplerSpec.MaxLOD = m_MipsLevels;
		SamplerSpec.ShaderResource = true;

		viewProp.SamplerSpec = SamplerSpec;

		m_ImageView = ResourceManager::CreateImageView(viewProp);
		Invalidate(format, width, height);
#if OLD_RENDERER
		TextureCube* instance = this;
		Renderer::Submit([instance]()
		{
			instance->Invalidate();
		});
#endif
	}
	TextureCube::~TextureCube()
	{
		Release();
	}

	void TextureCube::Release()
	{
		NV_CORE_WARN("TextureCube::Release ImageView ({1})", m_Properties.DebugName);
		// Main Image
		ResourceManager::DestroyImage(m_Image);
		ResourceManager::DestroyImageView(m_ImageView);
	}

	void TextureCube::GenerateMips(bool readonly)
	{
		auto device = RendererContext::GetCurrentDevice();
		auto vulkanDevice = device->GetVulkanDevice();

		VkCommandBuffer blitCmd = Renderer::GetCommandBuffer(true);
		uint32_t mipLevels = GetMipLevelCount();
		Image* image = ResourceManager::GetImage(m_Image);
		// Change each face (layer) layout to  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
		for (uint32_t face = 0; face < 6; face++)
		{
			VkImageSubresourceRange mipSubRange = {};
			mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			mipSubRange.baseMipLevel = 0;
			mipSubRange.baseArrayLayer = face;
			mipSubRange.levelCount = 1;
			mipSubRange.layerCount = 1;

			// Prepare current mip level as image blit destination
			image->InsertImageMemoryBarrier(blitCmd,
				0, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				mipSubRange);
		}
		// Generate the mips
		for (uint32_t i = 1; i < mipLevels; i++)
		{
			for (uint32_t face = 0; face < 6; face++)
			{
				VkImageBlit imageBlit = {};
				// Source
				imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.srcSubresource.layerCount = 1;
				imageBlit.srcSubresource.mipLevel = i - 1;
				imageBlit.srcSubresource.baseArrayLayer = face;
				// The region that data will be blitted from
				imageBlit.srcOffsets[0] = { 0, 0, 0 };
				imageBlit.srcOffsets[1].x = int32_t(m_Width >> (i - 1)); // Note: >> i <=> /2^i
				imageBlit.srcOffsets[1].y = int32_t(m_Height >> (i - 1));
				imageBlit.srcOffsets[1].z = 1; 	// 1 Since is a 2D image has depth of 1

				// Destination
				imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.dstSubresource.layerCount = 1;
				imageBlit.dstSubresource.mipLevel = i;
				imageBlit.dstSubresource.baseArrayLayer = face;
				// The region that data will be blitted to
				imageBlit.dstOffsets[0] = { 0, 0, 0 };
				imageBlit.dstOffsets[1].x = int32_t(m_Width >> i); // Note: >> i <=> /2^i
				imageBlit.dstOffsets[1].y = int32_t(m_Height >> i);
				imageBlit.dstOffsets[1].z = 1;	// 1 Since is a 2D image has depth of 1

				VkImageSubresourceRange mipSubRange = {};
				mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				mipSubRange.baseMipLevel = i;
				mipSubRange.baseArrayLayer = face;
				mipSubRange.levelCount = 1;
				mipSubRange.layerCount = 1;

				// Prepare current mip level as image blit destination
				image->InsertImageMemoryBarrier(blitCmd,
					0, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					mipSubRange);

				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Blit is transfer operation that allows to copying, scaling, and filtering. This operation depends
				// on the layout of the image it operates on. We could transition the entire image to VK_IMAGE_LAYOUT_GENERAL
				// but this will most likely be slow. For optimal performance, the source image should be in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
				// and the destination image should be in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

				// Blit from previous level. Note that info.Image is used for both the srcImage and dstImage  
				// parameter. This is because we're blitting between different levels of the same image.
				vkCmdBlitImage(
					blitCmd,
					image->GetHandle(),
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					image->GetHandle(),
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1,
					&imageBlit,
					VK_FILTER_LINEAR); //  We use the VK_FILTER_LINEAR to enable interpolation

				// Prepare current mip level as image blit source for next level
				image->InsertImageMemoryBarrier(blitCmd,
					VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					mipSubRange);
			}

		}

		// After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to SHADER_READ
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.layerCount = 6;
		subresourceRange.levelCount = mipLevels;

		image->InsertImageMemoryBarrier(blitCmd,
			VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
			readonly ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			subresourceRange);

		Renderer::FlushCommandBuffer(blitCmd);
	}

	VkImageView TextureCube::CreateImageViewSingleMip(uint32_t mip)
	{
#if OLD_RENDERER
		CORE_ASSERT(mip <= m_MipsLevels, "Mip level does not exists!");
		auto device = RendererContext::GetCurrentDevice();
		auto vulkanDevice = device->GetVulkanDevice();

		VkFormat format = utils::VulkanImageFormat(m_Format);

		VkImageViewCreateInfo view{};
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		view.format = format;
		view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = mip;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 6;
		view.subresourceRange.levelCount = 1;
		view.image = m_Info.Image;

		VkImageView result;
		VK_CHECK_RESULT(vkCreateImageView(vulkanDevice, &view, nullptr, &result));
		return result;
#endif
		return nullptr;
	}

	std::pair<uint32_t, uint32_t> TextureCube::GetMipSize(uint32_t mip) const
	{
		return std::pair<uint32_t, uint32_t>(uint32_t(m_Width >> mip), uint32_t(m_Height >> mip));
	}

	uint64_t TextureCube::GetHash() const
	{
		ImageView* imgView = ResourceManager::GetImageView(m_ImageView);
		if (!imgView)
		{
			CORE_ASSERT(false, "Invalid image view");
			return 0;
		}

		return (uint64_t)imgView->GetHandle();
	}

	void TextureCube::Invalidate(ImageFormat format, const uint32_t width, const uint32_t height)
	{
		// Get Devices
		auto device = RendererContext::GetCurrentDevice();
		auto vulkanDevice = device->GetVulkanDevice();
#if 0
		// Release data if exits
		Release();

		VkFormat vulkanFormat = utils::VulkanImageFormat(m_Format);
		uint32_t mipCount = GetMipLevelCount();
		ResourceAllocator allocator("TextureCube");
		/////////////////////////////////////////////////
		////// Create Image
		/////////////////////////////////////////////////
		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = vulkanFormat;
		imageCreateInfo.extent.width = m_Width;
		imageCreateInfo.extent.height = m_Height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = mipCount;
		imageCreateInfo.arrayLayers = 6;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL; // texels are laid out in an implementation-dependent arrangement, for more efficient memory access
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
		imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		m_Info.MemoryAlloc = allocator.AllocateImage(imageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY, m_Info.Image);
		/////////////////////////////////////////////////
		////// Create Image View
		/////////////////////////////////////////////////
		//  Cube map is in Vulkan – a 2D image with 6 different layers interpreted 
		//							as a cube through an image view.
		VkImageViewCreateInfo imageViewCreateInfo = {};
		imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		imageViewCreateInfo.format = vulkanFormat;
		imageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		imageViewCreateInfo.flags = 0;
		// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
		// It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
		imageViewCreateInfo.subresourceRange = {};
		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.levelCount = mipCount;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 6;
		imageViewCreateInfo.image = m_Info.Image;
		VK_CHECK_RESULT(vkCreateImageView(vulkanDevice, &imageViewCreateInfo, nullptr, &m_Info.ImageView));
		/////////////////////////////////////////////////
		////// Create Texture Smampler
		/////////////////////////////////////////////////
		VkSamplerCreateInfo samplerCreateInfo{};
		samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCreateInfo.magFilter = utils::VulkanSamplerFilter(m_Properties.SamplerFilter);	// How to render when the image is magnified on screen
		samplerCreateInfo.minFilter = utils::VulkanSamplerFilter(m_Properties.SamplerFilter);	// How to render when the image is minified on screen
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.addressModeU = utils::VulkanSamplerWrap(m_Properties.SamplerWrap);		// How to handle texture wrap in X (U) direction
		samplerCreateInfo.addressModeV = utils::VulkanSamplerWrap(m_Properties.SamplerWrap);		// How to handle texture wrap in Y (V) direction
		samplerCreateInfo.addressModeW = utils::VulkanSamplerWrap(m_Properties.SamplerWrap);		// How to handle texture wrap in Z (W) direction
		samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;													// Whether the coords should be normalize (0 to 1) (VK_FALSE = normalize coords)
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerCreateInfo.minLod = 0.0f;
		// Set max level-of-detail to mip level count of the texture
		samplerCreateInfo.maxLod = (float)mipCount;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;		// Border beyond texture (only work for texture clamp)
		// Enable anisotropic filtering
		// This feature is optional, so we must check if it's supported on the device
		if (m_Properties.Anisotropy)
		{
			samplerCreateInfo.maxAnisotropy = Renderer::GetDeviceLimits().maxSamplerAnisotropy;		// Anisotropy sample level
			samplerCreateInfo.anisotropyEnable = VK_TRUE;
		}
		else
		{
			samplerCreateInfo.maxAnisotropy = 1.f;		// Anisotropy sample level
			samplerCreateInfo.anisotropyEnable = VK_FALSE;
		}

		VK_CHECK_RESULT(vkCreateSampler(vulkanDevice, &samplerCreateInfo, nullptr, &m_Info.Sampler));


#endif
		Image* image = ResourceManager::GetImage(m_Image);
		ImageView* imageView = ResourceManager::GetImageView(m_ImageView);

		image->Invalidate();
		imageView->Invalidate();

		/////////////////////////////////////////////////
		////// Staging Buffer
		/////////////////////////////////////////////////
		// Copy data if present
		if (m_ImageData)
		{
			ResourceAllocator allocator("TextureCube");
			VkDeviceSize size = (uint64_t)m_ImageData.Size;
			// Create staging buffer
			// Create staging buffer to hold loaded data, ready to copy to device
			VkBuffer stagingBuffer;
			VkBufferCreateInfo bufferCreateInfo{};
			bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferCreateInfo.size = size;
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			// Allocate and Create Staging Buffer
			VmaAllocation stagingBufferAllocation = allocator.AllocateBuffer(bufferCreateInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer);
			// Copy data to staging buffer
			uint8_t* destData = allocator.MapMemory<uint8_t>(stagingBufferAllocation);
			//CORE_ASSERT(m_ImageData.Data, "Texture does not hold any data!");
			memcpy(destData, m_ImageData.Data, size);
			allocator.UnmapMemory(stagingBufferAllocation);

			// Get Copy/Transfer Command Buffer
			VkCommandBuffer copyCommandBuffer = Renderer::GetCommandBuffer(true);
			// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
			// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition 
			utils::ImageLayoutSpecification layoutSpec;
			layoutSpec.CommandBuffer = copyCommandBuffer;
			layoutSpec.Image = image->GetHandle();
			layoutSpec.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;					// Image only contains color data
			layoutSpec.OldImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			layoutSpec.NewImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			layoutSpec.SrcStageMask = VK_PIPELINE_STAGE_HOST_BIT;				// Source pipeline stage is host write/read exection (VK_PIPELINE_STAGE_HOST_BIT)
			layoutSpec.DstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;			// Destination pipeline stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
			layoutSpec.LayerCount = 6;
			// Changed image layout
			utils::SetImageLayout(layoutSpec);

			// Define Region of the Image to Copy 
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.bufferOffset = 0;											// Offset into data
			bufferCopyRegion.bufferRowLength = 0;										// Row length of data to calculate data spacing
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;	// Which aspect of image to copy
			bufferCopyRegion.imageSubresource.mipLevel = 0;								// Mipmap level to copy
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;						// Starting array layer (if array)
			bufferCopyRegion.imageSubresource.layerCount = 6;							// Number of layers to copy starting at baseArrayLayer
			bufferCopyRegion.imageExtent = { m_Width, m_Height, 1 };					// Size of region to copy as (x, y, z) values

			// Copy buffer to given image
			vkCmdCopyBufferToImage(copyCommandBuffer, stagingBuffer, image->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

			// Transition the texture image layout to  target, so we can safely copy our buffer data to it.
			// Once the data has been uploaded we transfer to the texture image to the VK_IMAGE_LAYOUT_GENERAL, so it can be write from and read to
			layoutSpec.CommandBuffer = copyCommandBuffer;
			layoutSpec.Image = image->GetHandle();
			layoutSpec.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;					// Image only contains color data
			layoutSpec.OldImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			layoutSpec.NewImageLayout = VK_IMAGE_LAYOUT_GENERAL;
			layoutSpec.SrcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;			// Source pipeline stage is host write/read exection (VK_PIPELINE_STAGE_HOST_BIT)
			layoutSpec.DstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;			// Destination pipeline stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
			layoutSpec.LayerCount = 6;

			image->SetImageLayout(layoutSpec);

			Renderer::FlushCommandBuffer(copyCommandBuffer);
			allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);
		}
		else
		{
			/**/
			// Get Copy/Transfer Command Buffer
			VkCommandBuffer copyCommandBuffer = Renderer::GetCommandBuffer(true);
			// Transition the texture image layout to  target, so we can safely copy our buffer data to it.
			// Once the data has been uploaded we transfer to the texture image to the VK_IMAGE_LAYOUT_GENERAL, so it can be write from, and read to
			utils::ImageLayoutSpecification layoutSpec;
			layoutSpec.CommandBuffer = copyCommandBuffer;
			layoutSpec.Image = image->GetHandle();
			layoutSpec.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;					// Image only contains color data
			layoutSpec.OldImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			layoutSpec.NewImageLayout = VK_IMAGE_LAYOUT_GENERAL;
			layoutSpec.SrcStageMask = VK_PIPELINE_STAGE_HOST_BIT;				// Source pipeline stage is host write/read exection (VK_PIPELINE_STAGE_HOST_BIT)
			layoutSpec.DstStageMask = VK_PIPELINE_STAGE_HOST_BIT;
			layoutSpec.LayerCount = 6;
			layoutSpec.LevelCount = m_MipsLevels;
			layoutSpec.BaseMipLevel = 0;

			image->SetImageLayout(layoutSpec);
			Renderer::FlushCommandBuffer(copyCommandBuffer);
			/**/
		}

#if 0
		// Update Descriptor
		// Image Layout when use
		m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		// Image to bind to set
		m_DescriptorImageInfo.imageView = m_Info.ImageView;
		// Sampler to use for set
		m_DescriptorImageInfo.sampler = m_Info.Sampler;
#endif

	}
	//////////////////////////////////////////
	/// TextureFactory
	//////////////////////////////////////////
	// function to convert decimal to hexadecimal
	std::string DecToHexa(uint32_t n)
	{
		// char array to store hexadecimal number
		char hexaDeciNum[2];

		// counter for hexadecimal number array
		int i = 0;
		while (n != 0) 
		{
			// temporary variable to store remainder
			int temp = 0;

			// storing remainder in temp variable.
			temp = n % 16;

			// check if temp < 10
			if (temp < 10) 
				hexaDeciNum[i] = temp + 48;
			else
				hexaDeciNum[i] = temp + 55;

			i++;
			n = n / 16;
		}

		std::string hexCode = "";
		if (i == 2) {
			hexCode.push_back(hexaDeciNum[0]);
			hexCode.push_back(hexaDeciNum[1]);
		}
		else if (i == 1) {
			hexCode = "0";
			hexCode.push_back(hexaDeciNum[0]);
		}
		else if (i == 0)
			hexCode = "00";

		// Return the equivalent
		// hexadecimal color code
		return hexCode;
	}

	// Function to convert the
	// RGB code to Hex color code
	std::uint32_t ConvertRGBtoHex(uint32_t R, uint32_t G, uint32_t B, uint32_t A)
	{
		CORE_ASSERT(((R >= 0 && R <= 255) && (G >= 0 && G <= 255) && (B >= 0 && B <= 255) && (A >= 0 && A <= 255)), "The hex color code doesn't exist");

		std::string hexCode = "";
		hexCode += DecToHexa(R);
		hexCode += DecToHexa(G);
		hexCode += DecToHexa(B);
		hexCode += DecToHexa(A);

		uint32_t result;
		std::stringstream ss;
		ss << std::hex << hexCode;
		ss >> result;

		return result;
	}

	Ref<Texture2D> TextureFactory::CreateBRDF()
	{
#if OLD_RENDERER
		uint32_t brdfSize = Renderer::GetRendererConfig().BrdfLutSize;
		ImageSpecification spec;
		spec.DebugName = "BRDF";
		spec.Format = ImageFormat::RG16F;
		spec.Height = brdfSize;
		spec.Width = brdfSize;
		spec.Mips = 1;
		spec.Usage = ImageUsage::Storage;
		spec.SamplerSpec.SamplerWrap = SamplerWrap::Clamp;
		Ref<Image2D> brdfImage = CreateRef<Image2D>(spec);
		Ref<Texture2D> brdfTex = CreateRef<Texture2D>(brdfImage);

		Ref<Shader> shader = Renderer::GetShaderLibrary().Get("BRDF");
		Ref<ComputePipeline> brdfPipeline = CreateRef<ComputePipeline>(shader);
		Renderer::Submit([brdfPipeline, shader, brdfImage, brdfTex, brdfSize]()
		{
			VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			brdfImage->Invalidate();
			// Descriptors
			VkWriteDescriptorSet writeDescriptor;
			VkDescriptorSet descriptorSet = Renderer::AllocateDescriptorSet(shader->GetDescriptorSetLayout(0));
			// Set descriptor for the Output Cubemap
			writeDescriptor = *shader->GetWriteDescriptorSet("o_LUT", 0);
			writeDescriptor.dstSet = descriptorSet;
			writeDescriptor.pImageInfo = &brdfImage->GetVulkanDescriptorInfo();
			// Update descriptors
			vkUpdateDescriptorSets(device, 1, &writeDescriptor, 0, NULL);
			// Compute Pass
			brdfPipeline->Begin();
			{
				uint32_t wordGroupXY = brdfSize / 32;
				brdfPipeline->Dispatch(wordGroupXY, wordGroupXY, 6, descriptorSet);
			}
			brdfPipeline->End();
		});

		return brdfTex;
#endif
		return nullptr;
	}
	Ref<Texture2D> TextureFactory::CreateTexture2DWithColor(const glm::ivec4& color)
	{
		uint32_t textureData = ConvertRGBtoHex(color.r, color.g, color.b, color.a);
		return CreateRef<Texture2D>(ImageFormat::RGBA, 1, 1, &textureData);
	}
	Ref<TextureCube> TextureFactory::CreateTextureCubeWithColor(const glm::vec4& color)
	{
		uint32_t textureData = ConvertRGBtoHex((float)color.r, (float)color.g, (float)color.b, (float)color.a);
		uint32_t cubeTextureData[6] = { textureData, textureData, textureData, textureData, textureData, textureData };
		TextureProperties prop;
		prop.DebugName = "Colored Texture Cube";
		return CreateRef<TextureCube>(ImageFormat::RGBA, 1, 1, prop, &cubeTextureData);
	}

	RenderTargetTexture::RenderTargetTexture(const RenderTargetProperties& rtProp)
		: m_Properties(rtProp)
	{
		Invalidate();
	}

	RenderTargetTexture::~RenderTargetTexture()
	{
		Release();
	}

	void RenderTargetTexture::Release()
	{
		ImageView* imgView = ResourceManager::GetImageView(m_ImageView);
		if (!imgView)
		{
			return;
		}

		Handle<Image> image;
		// if we own the handle then submit it for release, otherwise just leave it invalid it
		if (!m_Properties.ExistingImage.IsValid())
		{
			image = m_Image;
		}

		NV_CORE_WARN("RenderTargetTexture::Release ImageView ({1})", m_Properties.DebugName);
		// Main Image
		ResourceManager::DestroyImage(image); // If image is invalid the ResourceManager will ignore it
		ResourceManager::DestroyImageView(m_ImageView);
	}

	void RenderTargetTexture::Resize(uint32_t width, uint32_t height)
	{
		if (m_Properties.Resizable && (width != m_Properties.Width || height != m_Properties.Height))
		{
			m_Properties.Width = width;
			m_Properties.Height = height;
			Invalidate();
		}
	}

	std::pair<uint32_t, uint32_t> RenderTargetTexture::GetMipSize(uint32_t mip) const
	{
		return std::pair<uint32_t, uint32_t>(uint32_t(m_Properties.Width >> mip), uint32_t(m_Properties.Height >> mip));
	}

	uint64_t RenderTargetTexture::GetHash() const
	{
		ImageView* imgView = ResourceManager::GetImageView(m_ImageView);
		if (!imgView)
		{
			CORE_ASSERT(false, "Invalid image view");
			return 0;
		}

		return (uint64_t)imgView->GetHandle();
	}

	void RenderTargetTexture::Invalidate()
	{
		Release();
		Handle<Image> imgHandle = m_Properties.ExistingImage;
		Image* img = ResourceManager::GetImage(imgHandle);
		if (img == nullptr)
		{
			ImageSpecification spec;
			spec.DebugName = m_Properties.DebugName;
			spec.Width = m_Properties.Width;
			spec.Height = m_Properties.Height;


			spec.Format = m_Properties.Format;
			spec.Layers = m_Properties.LayerCount;
			spec.Usage = ImageUsage::Attachment;
			spec.Mips = m_Properties.MipCount;
			spec.Flags = m_Properties.ShaderResource ? SAMPLE_COMPATIBLE : 0;

			m_Image = imgHandle = ResourceManager::CreateImage(spec);
			img = ResourceManager::GetImage(imgHandle);
			img->Invalidate();
		}
		else
		{
			const auto& imgSpec = img->GetSpecification();
			CORE_ASSERT(img->GetWidth() == m_Properties.Width && img->GetHeight() == m_Properties.Height 
							&& imgSpec.Usage == ImageUsage::Attachment,
				"Exiting image does not have the proper size of usage for the Framebuffer!")
		}

		ImageViewProperties viewProp;
		viewProp.Image = imgHandle;
		viewProp.BaseLayer = m_Properties.BaseLayer;
		viewProp.BaseMip = m_Properties.BaseMip;
		viewProp.LayerCount = m_Properties.LayerCount;
		viewProp.MipCount = m_Properties.MipCount;
		viewProp.ViewType = m_Properties.ViewType;

		SamplerSpecification SamplerSpec;
		// TODO - SORT the resource stuff
		SamplerSpec.ShaderResource = true;
		if (true || m_Properties.ShaderResource)
		{
			SamplerSpec.SamplerWrap = SamplerWrap::Clamp;
			SamplerSpec.SamplerFilter = SamplerFilter::Linear;
			SamplerSpec.Anisotropy = false;
			SamplerSpec.MinLOD = 0.f;
			SamplerSpec.MaxLOD = m_Properties.MipCount;
			SamplerSpec.ShaderResource = m_Properties.ShaderResource;
			
		}
		viewProp.SamplerSpec = SamplerSpec;
		
		m_ImageView = ResourceManager::CreateImageView(viewProp);
		ImageView* imgView = ResourceManager::GetImageView(m_ImageView);
		imgView->Invalidate();


	}

}