#pragma once
#include "Renderer/Image.h"
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

	// Asset size texture that cotains all the disk data and metadata to load the texture "resource"
	class TextureAsset : public Asset
	{
	public:
		TextureAsset(const TextureMetadata& metadata);
		~TextureAsset();

		static AssetType GetStaticType() { return AssetType::Texture; }
		AssetType GetAssetType() const { return GetStaticType(); }

		const ImageFormat GetFormat() const { return m_Format; }
		const uint32_t GetWidth() const { return m_Width; }
		const uint32_t GetHeight() const { return m_Height; }
		const Buffer& GetWriteableBuffer() const { return m_ImageData; }
		const std::filesystem::path& GetPath() const { return m_Metadata.FilePath; }
		const TextureMetadata& GetTextureMetadata() const { return m_Metadata; }

		bool Loaded() const { return m_ImageData; }
	private:
		TextureMetadata m_Metadata;
		uint32_t m_Width;
		uint32_t m_Height;
		ImageFormat m_Format;
		Buffer m_ImageData;
	};
}