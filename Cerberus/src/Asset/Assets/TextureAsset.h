#pragma once
#include "Renderer/RHI/Image.h"
#include "Asset/Asset.h"
namespace cerberus
{
	enum class TextureType : uint8_t
	{
		Texture2D = 0,
		TextureCube = 1,
		Default = 0
	};
	struct TextureMetadata
	{
		std::string DebugName = "None";
		std::filesystem::path FilePath;
		
		TextureType TextureType = TextureType::Default;
		SamplerWrap SamplerWrap = SamplerWrap::Repeat;
		SamplerFilter SamplerFilter = SamplerFilter::Linear;

		bool GenerateMips = true;
		bool SRGB = false;
		bool Storage = false;
		bool Anisotropy = false;
	};

	// Asset size texture that contains all the disk data and metadata to load the texture "resource"
	class TextureAsset : public Asset
	{
	public:
		TextureAsset(const TextureMetadata& metadata);
		TextureAsset(const TextureAsset&) = delete;
		TextureAsset& operator=(const TextureAsset&) = delete;
		~TextureAsset();

		static AssetType GetStaticType() { return AssetType::Texture; }
		AssetType GetAssetType() const { return GetStaticType(); }

		ImageFormat GetFormat() const { return m_Format; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		const Buffer& GetPixelData() const { return m_ImageData; }
		const std::filesystem::path& GetPath() const { return m_Metadata.FilePath; }
		const TextureMetadata& GetTextureMetadata() const { return m_Metadata; }

		bool Loaded() const { return m_ImageData && m_Width > 0 && m_Height > 0 && m_Format != ImageFormat::None; }
	private:
		TextureMetadata m_Metadata;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		ImageFormat m_Format = ImageFormat::None;
		Buffer m_ImageData;
	};
}
