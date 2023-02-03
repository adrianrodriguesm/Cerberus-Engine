#include "pch.h"
#include "TextureAsset.h"
#include "stb_image.h"
namespace cerberus
{
	TextureAsset::TextureAsset(const TextureMetadata& metadata)
		: m_Metadata(metadata)
	{
		int width, height, channels;
		const auto& filePath = m_Metadata.FilePath.string().c_str();
		// Check if it is an HDR image
		if (stbi_is_hdr(filePath))
		{
			m_ImageData.Data = (byte*)stbi_loadf(filePath, &width, &height, &channels, 4);
			m_ImageData.Size = width * height * 4 * sizeof(float); // 4 = number of channels
			m_Format = ImageFormat::RGBA32F;
		}
		else
		{
			m_ImageData.Data = stbi_load(filePath, &width, &height, &channels, 4);
			m_ImageData.Size = width * height * 4;
			m_Format = m_Metadata.SRGB ? ImageFormat::SRGBA : ImageFormat::RGBA;
		}

		// Check if the image was succesfully loaded
		if (!m_ImageData.Data)
		{
			CORE_ASSERT(false, "Falied to load image!");
			return;
		}

		m_Width = width;
		m_Height = height;
	}
	TextureAsset::~TextureAsset()
	{
		if (m_ImageData)
			m_ImageData.Release();
	}
}