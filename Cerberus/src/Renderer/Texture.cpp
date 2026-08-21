#include "pch.h"
#include "Texture.h"
#include "Renderer/Renderer.h"
#include "Renderer/ComputeCommandList.h"
#include "Renderer/TransferCommandList.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/TextureAsset.h"
#include "Renderer/RendererResources.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/ShaderCompiler.h"
namespace cerberus
{
	namespace
	{
		const TextureAsset& RequireLoadedTextureAsset(const Ref<TextureAsset>& textureAsset)
		{
			CORE_ASSERT(textureAsset && textureAsset->Loaded(), "Texture2D requires a loaded TextureAsset");
			return *textureAsset;
		}

		bool ResolveTextureSRGB(const TextureMetadata& metadata, TextureColorSpace colorSpace)
		{
			switch (colorSpace)
			{
			case TextureColorSpace::Linear: return false;
			case TextureColorSpace::SRGB: return true;
			case TextureColorSpace::AssetDefault: return metadata.SRGB;
			}

			CORE_ASSERT(false, "Unknown TextureColorSpace");
			return metadata.SRGB;
		}

		TextureProperties BuildTextureProperties(const TextureMetadata& metadata, TextureColorSpace colorSpace)
		{
			TextureProperties properties;
			properties.DebugName = metadata.DebugName;
			properties.SamplerWrap = metadata.SamplerWrap;
			properties.SamplerFilter = metadata.SamplerFilter;
			properties.GenerateMips = metadata.GenerateMips;
			properties.SRGB = ResolveTextureSRGB(metadata, colorSpace);
			properties.Storage = metadata.Storage;
			properties.Anisotropy = metadata.Anisotropy;
			return properties;
		}

		ImageFormat ResolveTextureFormat(const TextureAsset& textureAsset, bool srgb)
		{
			ImageFormat format = textureAsset.GetFormat();
			if (format == ImageFormat::RGBA || format == ImageFormat::SRGBA)
				return srgb ? ImageFormat::SRGBA : ImageFormat::RGBA;

			return format;
		}
	}

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
		Initialize(format, width, height, data);
	}

	Texture2D::Texture2D(const Ref<TextureAsset>& textureAsset, TextureColorSpace colorSpace)
	{
		const TextureAsset& sourceAsset = RequireLoadedTextureAsset(textureAsset);
		const TextureMetadata& metadata = sourceAsset.GetTextureMetadata();
		m_Properties = BuildTextureProperties(metadata, colorSpace);
		m_TextureAsset = sourceAsset.Handle;

		Initialize(
			ResolveTextureFormat(sourceAsset, ResolveTextureSRGB(metadata, colorSpace)),
			sourceAsset.GetWidth(),
			sourceAsset.GetHeight(),
			sourceAsset.GetPixelData().Data);
	}

	void Texture2D::Initialize(ImageFormat format, uint32_t width, uint32_t height, const void* data)
	{
		uint32_t imageSize = Image::GetImageMemorySize(format, width, height);
		if (data && imageSize)
			m_ImageData = Buffer::Copy(data, imageSize);

		CORE_ASSERT(m_ImageData.Data || m_Properties.Storage, "Texture2D requires source data unless it is a storage texture");
		CORE_ASSERT(!m_Properties.Storage || !m_Properties.GenerateMips, "Storage Texture2D mip generation is not supported yet");

		ImageSpecification imageSpec;
		imageSpec.DebugName = m_Properties.DebugName;
		imageSpec.Usage = m_Properties.Storage ? ImageUsage::Storage : ImageUsage::Sampler;
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
	}

	Texture2D::~Texture2D()
	{
		ResourceManager::DestroyImageView(m_ImageView);
		ResourceManager::DestroyImage(m_Image);

		if (m_ImageData)
			m_ImageData.Release();
	}

	Texture2D::Texture2D(Texture2D&& other) noexcept
		: m_Properties(std::move(other.m_Properties))
		, m_ImageData(other.m_ImageData)
		, m_Image(other.m_Image)
		, m_ImageView(other.m_ImageView)
		, m_TextureAsset(other.m_TextureAsset)
	{
		other.m_ImageData = Buffer{};
		other.m_Image = Handle<Image>{};
		other.m_ImageView = Handle<ImageView>{};
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
		SamplerSpecification samplerSpec{};
		samplerSpec.SamplerWrap = m_Properties.SamplerWrap;
		samplerSpec.SamplerFilter = m_Properties.SamplerFilter;
		samplerSpec.Anisotropy = m_Properties.Anisotropy;
		samplerSpec.MinLOD = 0.0f;
		samplerSpec.MaxLOD = static_cast<float>(imageSpec.Mips);
		samplerSpec.ShaderResource = true;
		viewProp.Image = m_Image;
		viewProp.SamplerSpec = samplerSpec;
		imgView->Invalidate();

		if (!m_ImageData.Data)
		{
			CORE_ASSERT(m_Properties.Storage, "Texture2D without source data must be a storage texture");
			TransientCommandBuffer layoutCommandBuffer = Renderer::GetTransientCommandExecutor().BeginGraphics();
			TransferCommandList transfer(layoutCommandBuffer.GetRecordingView());
			VkImageSubresourceRange subresourceRange{};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.levelCount = imageSpec.Mips;
			subresourceRange.layerCount = 1;
			image->InsertImageMemoryBarrier(
				transfer,
				0,
				VK_ACCESS_SHADER_WRITE_BIT,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				subresourceRange);
			layoutCommandBuffer.SubmitAndWait();
			imgView->UpdateDescriptor();
			return;
		}

		VkImage vkImage = image->GetHandle();

		VkDeviceSize size = (uint64_t)m_ImageData.Size;
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
		TransientCommandBuffer copyCommandBuffer = Renderer::GetTransientCommandExecutor().BeginGraphics();
		TransferCommandList transfer(copyCommandBuffer.GetRecordingView());

		// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition 
		// Source pipeline stage is host write/read exection (VK_PIPELINE_STAGE_HOST_BIT)
		// Destination pipeline stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
		image->SetLayout(transfer,
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
		transfer.CopyBufferToImage(
			stagingBuffer,
			vkImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			std::span<const VkBufferImageCopy>(&bufferCopyRegion, 1));
		
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
			dstPipelineStgFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		}

		// Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
		image->SetLayout(transfer, dstLayout,
			VK_PIPELINE_STAGE_TRANSFER_BIT, dstPipelineStgFlags);


		// Flush Copy/Transfer Command Buffer (not longer needed)
		copyCommandBuffer.SubmitAndWait();
		allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);

		if (m_Properties.GenerateMips)
			GenerateMips();

		// TextureAsset owns persistent source pixels. Texture2D only keeps this
		// temporary copy for the staging upload.
		m_ImageData.Release();
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
		TransientCommandBuffer blitCmd = Renderer::GetTransientCommandExecutor().BeginGraphics();
		TransferCommandList transfer(blitCmd.GetRecordingView());

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
			transfer.ImageBarrier(vkImage,
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
			transfer.BlitImage(
				vkImage,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				vkImage,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				std::span<const VkImageBlit>(&imageBlit, 1),
				VK_FILTER_LINEAR); //  We use the VK_FILTER_LINEAR to enable interpolation

			// Prepare current mip level as image blit source for next level
			transfer.ImageBarrier(vkImage,
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

		image->InsertImageMemoryBarrier(transfer,
			VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			subresourceRange);

		blitCmd.SubmitAndWait();

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
		imageSpec.Usage = m_Properties.Storage ? ImageUsage::Storage : ImageUsage::Sampler;
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
	}
	TextureCube::~TextureCube()
	{
		Release();
	}

	TextureCube::TextureCube(TextureCube&& other) noexcept
		: m_Format(other.m_Format)
		, m_Width(other.m_Width)
		, m_Height(other.m_Height)
		, m_Properties(std::move(other.m_Properties))
		, m_MipsLevels(other.m_MipsLevels)
		, m_Image(other.m_Image)
		, m_ImageView(other.m_ImageView)
		, m_TextureAsset(other.m_TextureAsset)
		, m_ImageData(other.m_ImageData)
	{
		other.m_Image = Handle<Image>{};
		other.m_ImageView = Handle<ImageView>{};
		other.m_ImageData = Buffer{};
	}

	void TextureCube::Release()
	{
		NV_CORE_WARN("TextureCube::Release ImageView ({1})", m_Properties.DebugName);
		// Vulkan image views must be released before their backing image.
		ResourceManager::DestroyImageView(m_ImageView);
		ResourceManager::DestroyImage(m_Image);
	}

	void TextureCube::GenerateMips(bool readonly)
	{
		TransientCommandBuffer blitCmd = Renderer::GetTransientCommandExecutor().BeginGraphics();
		TransferCommandList transfer(blitCmd.GetRecordingView());
		uint32_t mipLevels = GetMipLevelCount();
		Image* image = ResourceManager::GetImage(m_Image);
		CORE_ASSERT(image, "TextureCube has an invalid image");

		// Image tracks a whole-resource layout, while mip generation needs explicit
		// subresource transitions. Transition all six faces of each mip together and
		// leave the complete image in one coherent final layout.
		VkImageSubresourceRange baseMipRange{};
		baseMipRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		baseMipRange.baseMipLevel = 0;
		baseMipRange.levelCount = 1;
		baseMipRange.baseArrayLayer = 0;
		baseMipRange.layerCount = 6;

		transfer.ImageBarrier(
			image->GetHandle(),
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			baseMipRange);

		// Generate the mips
		for (uint32_t i = 1; i < mipLevels; i++)
		{
			VkImageSubresourceRange mipSubRange{};
			mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			mipSubRange.baseMipLevel = i;
			mipSubRange.baseArrayLayer = 0;
			mipSubRange.levelCount = 1;
			mipSubRange.layerCount = 6;

			transfer.ImageBarrier(
				image->GetHandle(),
				0,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				mipSubRange);

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

				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Blit is transfer operation that allows to copying, scaling, and filtering. This operation depends
				// on the layout of the image it operates on. We could transition the entire image to VK_IMAGE_LAYOUT_GENERAL
				// but this will most likely be slow. For optimal performance, the source image should be in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
				// and the destination image should be in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

				// Blit from previous level. Note that info.Image is used for both the srcImage and dstImage  
				// parameter. This is because we're blitting between different levels of the same image.
				transfer.BlitImage(
					image->GetHandle(),
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					image->GetHandle(),
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					std::span<const VkImageBlit>(&imageBlit, 1),
					VK_FILTER_LINEAR); //  We use the VK_FILTER_LINEAR to enable interpolation

			}

			transfer.ImageBarrier(
				image->GetHandle(),
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				mipSubRange);

		}

		// After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to SHADER_READ
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.layerCount = 6;
		subresourceRange.levelCount = mipLevels;

		image->InsertImageMemoryBarrier(
			transfer,
			VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			readonly ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			subresourceRange);

		blitCmd.SubmitAndWait();
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
			memcpy(destData, m_ImageData.Data, size);
			allocator.UnmapMemory(stagingBufferAllocation);

			// Get Copy/Transfer Command Buffer
			TransientCommandBuffer copyCommandBuffer = Renderer::GetTransientCommandExecutor().BeginGraphics();
			TransferCommandList transfer(copyCommandBuffer.GetRecordingView());
			// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
			// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition 
			VkImageSubresourceRange uploadRange{};
			uploadRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			uploadRange.baseMipLevel = 0;
			uploadRange.levelCount = 1;
			uploadRange.baseArrayLayer = 0;
			uploadRange.layerCount = 6;
			image->SetLayout(
				transfer,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_HOST_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				uploadRange);

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
			transfer.CopyBufferToImage(
				stagingBuffer,
				image->GetHandle(),
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				std::span<const VkBufferImageCopy>(&bufferCopyRegion, 1));

			// Transition to the layout advertised by the ImageView descriptor.
			const VkImageLayout finalLayout = m_Properties.Storage
				? VK_IMAGE_LAYOUT_GENERAL
				: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			image->SetLayout(
				transfer,
				finalLayout,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				m_Properties.Storage
				? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
				: VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				uploadRange);

			copyCommandBuffer.SubmitAndWait();
			allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);
		}
		else
		{
			// Get Copy/Transfer Command Buffer
			TransientCommandBuffer copyCommandBuffer = Renderer::GetTransientCommandExecutor().BeginGraphics();
			TransferCommandList transfer(copyCommandBuffer.GetRecordingView());
			const VkImageLayout finalLayout = m_Properties.Storage
				? VK_IMAGE_LAYOUT_GENERAL
				: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkImageSubresourceRange fullRange{};
			fullRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			fullRange.baseMipLevel = 0;
			fullRange.levelCount = m_MipsLevels;
			fullRange.baseArrayLayer = 0;
			fullRange.layerCount = 6;
			image->SetLayout(
				transfer,
				finalLayout,
				VK_PIPELINE_STAGE_HOST_BIT,
				m_Properties.Storage
				? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
				: VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				fullRange);
			copyCommandBuffer.SubmitAndWait();
		}

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

	Handle<Texture2D> TextureFactory::CreateBRDF()
	{
		const uint32_t brdfSize = Renderer::GetRendererConfig().BrdfLutSize;

		TextureProperties properties;
		properties.DebugName = "BRDF LUT";
		properties.SamplerWrap = SamplerWrap::Clamp;
		properties.SamplerFilter = SamplerFilter::Linear;
		properties.GenerateMips = false;
		properties.Storage = true;

		Handle<Texture2D> brdfTexture = ResourceManager::CreateTexture2D(
			ImageFormat::RG16F,
			brdfSize,
			brdfSize,
			nullptr,
			properties);

		Texture2D* texture = ResourceManager::GetTexture2D(brdfTexture);
		CORE_ASSERT(texture, "Failed to create BRDF LUT Texture2D");

		Ref<Shader> shader = RendererResources::GetShaderLibrary().Get("BRDF");
		CORE_ASSERT(shader, "BRDF compute shader is not loaded");

		BindGroupLayout::BindGroupLayoutInfo layoutInfo = ShaderReflection::ReflectLayoutInfo(shader, 0);
		CORE_ASSERT(!layoutInfo.ImageStorages.empty(), "BRDF shader has no storage-image binding");

		uint32_t outputBinding = layoutInfo.ImageStorages.front().BindingPoint;
		for (const BindGroupLayout::ImageBinding& binding : layoutInfo.ImageStorages)
		{
			if (binding.Name == "o_LUT")
			{
				outputBinding = binding.BindingPoint;
				break;
			}
		}

		Handle<BindGroupLayout> bindGroupLayout = ResourceManager::CreateBindGroupLayout(layoutInfo);
		BindGroupSpecification bindGroupSpec;
		bindGroupSpec.BindGroupLayout = bindGroupLayout;
		bindGroupSpec.Lifetime = DescriptorAllocationLifetime::Transient;
		Handle<BindGroup> bindGroupHandle = ResourceManager::CreateBindGroup(bindGroupSpec);

		BindGroup* bindGroup = ResourceManager::GetBindGroup(bindGroupHandle);
		CORE_ASSERT(bindGroup, "Failed to create BRDF bind group");
		bindGroup->SetImageView(texture->GetImageView(), outputBinding);
		bindGroup->Update(0);

		Handle<ComputePipeline> brdfPipeline = ResourceManager::CreateComputePipeline(shader);
		RenderQueueManager::ExecuteRenderQueue();

		TransientCommandBuffer commandBuffer = Renderer::GetTransientCommandExecutor().BeginCompute();
		ComputeCommandList compute(commandBuffer.GetRecordingView());
		compute.BindPipeline(brdfPipeline);
		compute.BindDescriptorSets(bindGroupHandle);
		const uint32_t workGroupCount = (brdfSize + 31u) / 32u;
		compute.Dispatch(workGroupCount, workGroupCount, 1);
		commandBuffer.SubmitAndWait();

		ResourceManager::DestroyComputePipeline(brdfPipeline);
		ResourceManager::DestroyBindGroup(bindGroupHandle);
		ResourceManager::DestroyBindGroupLayout(bindGroupLayout);
		return brdfTexture;
	}

	Handle<Texture2D> TextureFactory::CreateTexture2DWithColor(const glm::ivec4& color)
	{
		uint32_t textureData = ConvertRGBtoHex(color.r, color.g, color.b, color.a);
		return ResourceManager::CreateTexture2D(ImageFormat::RGBA, 1, 1, &textureData);
	}

	Handle<TextureCube> TextureFactory::CreateTextureCubeWithColor(const glm::vec4& color)
	{
		uint32_t textureData = ConvertRGBtoHex((float)color.r, (float)color.g, (float)color.b, (float)color.a);
		uint32_t cubeTextureData[6] = { textureData, textureData, textureData, textureData, textureData, textureData };
		TextureProperties prop;
		prop.DebugName = "Colored Texture Cube";
		return ResourceManager::CreateTextureCube(ImageFormat::RGBA, 1, 1, prop, &cubeTextureData);
	}

	RenderTargetTexture::RenderTargetTexture(const RenderTargetProperties& rtProp)
		: m_Properties(rtProp)
	{
		Invalidate();
	}

	RenderTargetTexture::RenderTargetTexture(RenderTargetTexture&& other) noexcept
		: m_Image(other.m_Image)
		, m_ImageView(other.m_ImageView)
		, m_Properties(std::move(other.m_Properties))
	{
		other.m_Image = Handle<Image>{};
		other.m_ImageView = Handle<ImageView>{};
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
