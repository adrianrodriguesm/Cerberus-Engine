#include "pch.h"
#include "TextureAsset.h"
#include "stb_image.h"
#include <limits>
namespace cerberus
{
	TextureAsset::TextureAsset(const TextureMetadata& metadata)
		: m_Metadata(metadata)
	{
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
