#include "pch.h"
#include "TextureAsset.h"
#include "stb_image.h"
#include <dds_image/include/dds.hpp>
#include <limits>
namespace cerberus
{
	namespace
	{
		ImageFormat ImageFormatFromDDS(const dds::Image& image)
		{
			switch (dds::getVulkanFormat(image.format, image.supportsAlpha))
			{
			case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
			case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return ImageFormat::BC1_RGBA;
			case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
			case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:  return ImageFormat::BC1_SRGBA;
			case VK_FORMAT_BC2_UNORM_BLOCK:      return ImageFormat::BC2_RGBA;
			case VK_FORMAT_BC2_SRGB_BLOCK:       return ImageFormat::BC2_SRGBA;
			case VK_FORMAT_BC3_UNORM_BLOCK:      return ImageFormat::BC3_RGBA;
			case VK_FORMAT_BC3_SRGB_BLOCK:       return ImageFormat::BC3_SRGBA;
			case VK_FORMAT_BC4_UNORM_BLOCK:      return ImageFormat::BC4_UNORM;
			case VK_FORMAT_BC4_SNORM_BLOCK:      return ImageFormat::BC4_SNORM;
			case VK_FORMAT_BC5_UNORM_BLOCK:      return ImageFormat::BC5_UNORM;
			case VK_FORMAT_BC5_SNORM_BLOCK:      return ImageFormat::BC5_SNORM;
			case VK_FORMAT_BC6H_UFLOAT_BLOCK:    return ImageFormat::BC6H_UFLOAT;
			case VK_FORMAT_BC6H_SFLOAT_BLOCK:    return ImageFormat::BC6H_SFLOAT;
			case VK_FORMAT_BC7_UNORM_BLOCK:      return ImageFormat::BC7_RGBA;
			case VK_FORMAT_BC7_SRGB_BLOCK:       return ImageFormat::BC7_SRGBA;
			default:                              return ImageFormat::None;
			}
		}
	}

	TextureAsset::TextureAsset(const TextureMetadata& metadata)
		: m_Metadata(metadata)
	{
		if (m_Metadata.FilePath.extension() == ".dds" || m_Metadata.FilePath.extension() == ".DDS")
		{
			dds::Image ddsImage;
			const dds::ReadResult result = dds::readFile(m_Metadata.FilePath.string(), &ddsImage);
			if (result != dds::ReadResult::Success)
			{
				NV_CORE_ERROR("Failed to load DDS texture {0} (result {1})", m_Metadata.FilePath.string(), static_cast<int>(result));
				return;
			}
			if (ddsImage.dimension != dds::Texture2D || ddsImage.arraySize != 1 || ddsImage.depth != 1)
			{
				NV_CORE_ERROR("Only 2D, single-layer DDS material textures are supported: {0}", m_Metadata.FilePath.string());
				return;
			}
			m_Format = ImageFormatFromDDS(ddsImage);
			if (m_Format == ImageFormat::None || ddsImage.mipmaps.empty())
			{
				NV_CORE_ERROR("Unsupported DDS texture format: {0}", m_Metadata.FilePath.string());
				return;
			}

			uint64_t totalSize = 0;
			for (const auto& mip : ddsImage.mipmaps)
				totalSize += mip.size();
			if (totalSize > std::numeric_limits<uint32_t>::max())
			{
				NV_CORE_ERROR("DDS texture exceeds Buffer capacity: {0}", m_Metadata.FilePath.string());
				return;
			}

			m_ImageData.Allocate(static_cast<uint32_t>(totalSize));
			uint32_t offset = 0;
			uint32_t mipWidth = ddsImage.width;
			uint32_t mipHeight = ddsImage.height;
			for (const auto& mip : ddsImage.mipmaps)
			{
				const uint32_t mipSize = static_cast<uint32_t>(mip.size());
				m_ImageData.Write(mip.data(), mipSize, offset);
				m_MipData.push_back({ offset, mipSize, mipWidth, mipHeight });
				offset += mipSize;
				mipWidth = glm::max(1u, mipWidth / 2u);
				mipHeight = glm::max(1u, mipHeight / 2u);
			}

			m_Width = ddsImage.width;
			m_Height = ddsImage.height;
			NV_CORE_INFO("Loaded DDS {0}: {1}x{2}, {3} mips, VkFormat {4}",
				m_Metadata.FilePath.string(), m_Width, m_Height, m_MipData.size(),
				static_cast<int>(utils::VulkanImageFormat(m_Format)));
			return;
		}

		int width = 0;
		int height = 0;
		int channels = 0;
		std::string strFilePath = m_Metadata.FilePath.string();
		const auto& filePath = strFilePath.c_str();
		void* decodedPixels = nullptr;
		uint32_t bytesPerPixel = 0;

		const bool isHDR = stbi_is_hdr(filePath) != 0;
		if (isHDR)
		{
			decodedPixels = stbi_loadf(filePath, &width, &height, &channels, 4);
			bytesPerPixel = 4 * sizeof(float);
			m_Format = ImageFormat::RGBA32F;
		}
		else
		{
			decodedPixels = stbi_load(filePath, &width, &height, &channels, 4);
			bytesPerPixel = 4;
			m_Format = m_Metadata.SRGB ? ImageFormat::SRGBA : ImageFormat::RGBA;
		}

		if (!decodedPixels)
		{
			CORE_ASSERT(false, "Failed to load texture image");
			return;
		}

		const uint64_t decodedSize = static_cast<uint64_t>(width) * height * bytesPerPixel;
		if (decodedSize > std::numeric_limits<uint32_t>::max())
		{
			stbi_image_free(decodedPixels);
			CORE_ASSERT(false, "TextureAsset exceeds Buffer size capacity");
			return;
		}

		// stb_image owns the decode allocation. Copy into Cerberus Buffer storage so
		// TextureAsset has one allocator/deallocator pair for both LDR and HDR data.
		m_ImageData = Buffer::Copy(decodedPixels, static_cast<uint32_t>(decodedSize));
		stbi_image_free(decodedPixels);

		m_Width = width;
		m_Height = height;
	}
	TextureAsset::~TextureAsset()
	{
		if (m_ImageData)
			m_ImageData.Release();
	}
}
