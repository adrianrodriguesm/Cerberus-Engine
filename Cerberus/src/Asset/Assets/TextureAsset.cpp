#include "pch.h"
#include "TextureAsset.h"
#include "stb_image.h"
#include <dds_image/include/dds.hpp>
#include <limits>
namespace cerberus
{
	namespace
	{
		// Inspect top-level BC alpha selectors, rather than treating every RGBA
		// container as masked (many ordinary albedos contain only alpha=255).
		template<typename Pixels>
		bool HasBCAlpha(const Pixels& pixels, ImageFormat format)
		{
			const bool bc1 = format == ImageFormat::BC1_RGBA || format == ImageFormat::BC1_SRGBA;
			const bool bc2 = format == ImageFormat::BC2_RGBA || format == ImageFormat::BC2_SRGBA;
			const bool bc3 = format == ImageFormat::BC3_RGBA || format == ImageFormat::BC3_SRGBA;
			if (!bc1 && !bc2 && !bc3)
				return false;
			const size_t stride = bc1 ? 8 : 16;
			for (size_t offset = 0; offset + stride <= pixels.size(); offset += stride)
			{
				const auto* block = pixels.data() + offset;
				if (bc1)
				{
					const uint16_t c0 = uint16_t(block[0]) | (uint16_t(block[1]) << 8);
					const uint16_t c1 = uint16_t(block[2]) | (uint16_t(block[3]) << 8);
					if (c0 <= c1)
						for (uint32_t i = 0; i < 16; ++i)
							if (((block[4 + i / 4] >> (2 * (i % 4))) & 3) == 3)
								return true;
				}
				else if (bc2)
				{
					for (uint32_t i = 0; i < 8; ++i)
						if (block[i] != 255) return true;
				}
				else
				{
					uint64_t selectors = 0;
					for (uint32_t i = 0; i < 6; ++i)
						selectors |= uint64_t(block[2 + i]) << (8 * i);
					for (uint32_t i = 0; i < 16; ++i)
					{
						const uint32_t selector = (selectors >> (3 * i)) & 7;
						const bool opaque = selector == 0 ? block[0] == 255
							: selector == 1 ? block[1] == 255
							: (block[0] <= block[1] && selector == 7)
								|| (block[0] == 255 && block[1] == 255 && selector <= 5);
						if (!opaque) return true;
					}
				}
			}
			return false;
		}

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
			m_HasAlphaContent = HasBCAlpha(ddsImage.mipmaps.front(), m_Format);
			// BC1 RGB uses selector 3 as black, while BC1 RGBA uses it as
			// transparent. The DDS alpha flag distinguishes authoring intent.
			if ((m_Format == ImageFormat::BC1_RGBA || m_Format == ImageFormat::BC1_SRGBA) && !ddsImage.supportsAlpha)
				m_HasAlphaContent = false;
			// BC7 alpha intent is supplied by the DDS header; no BC7 CPU decoder.
			if (m_Format == ImageFormat::BC7_RGBA || m_Format == ImageFormat::BC7_SRGBA)
				m_HasAlphaContent = ddsImage.supportsAlpha;
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

		if (!isHDR)
		{
			const auto* pixels = static_cast<const uint8_t*>(decodedPixels);
			for (uint64_t i = 3; i < decodedSize; i += 4)
				if (pixels[i] != 255) { m_HasAlphaContent = true; break; }
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
