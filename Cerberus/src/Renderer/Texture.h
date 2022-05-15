#pragma once
#include "Renderer/Image.h"
#include "Core/Buffer.h"
#include "Asset/Asset.h"
namespace cerberus
{
	struct TextureProperties
	{
		SamplerWrap SamplerWrap = SamplerWrap::Repeat;
		SamplerFilter SamplerFilter = SamplerFilter::Linear;
		bool GenerateMips = true;
		bool SRGB = false;
		bool Storage = false;
		bool Anisotropy = false;
		std::string DebugName = "None";
	};

	class Texture : public Asset
	{
	public:
		virtual ~Texture() {}
		virtual ImageFormat GetFormat() const = 0;
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual uint32_t GetMipLevelCount() const = 0;
		virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const = 0;
		virtual uint64_t GetHash() const = 0;

		static AssetType GetStaticType() { return AssetType::Texture; }
		AssetType GetAssetType() const { return GetStaticType(); }
	};
	////////////////////////////////////////////////
	////// Texture2D
	////////////////////////////////////////////////
	class Texture2D : public Texture
	{
	public:
		Texture2D(const std::string& path, TextureProperties properties);
		Texture2D(ImageFormat format, const uint32_t width, const uint32_t height, const void* data, TextureProperties properties = {});
		Texture2D(const Ref<Image2D>& image2D);
		virtual ~Texture2D();


		Ref<Image2D> GetImage() const { return m_Image; }
		virtual ImageFormat GetFormat() const override { return m_Format; }
		virtual uint32_t GetWidth() const override { return m_Width; }
		virtual uint32_t GetHeight() const override { return m_Height; }
		virtual uint64_t GetHash() const override { return (uint64_t)m_Image->GetImageInfo().ImageView; };
		const VkDescriptorImageInfo& GetVulkanDescriptorInfo() const { return m_Image->GetVulkanDescriptorInfo(); }
		const Buffer& GetWriteableBuffer() const { return m_ImageData; }
		bool Loaded() const { return m_ImageData; }
		const std::string& GetPath() const { return m_Path; }
		const TextureProperties& GetTextureProperties() const { return m_Properties; }
		uint32_t GetMipLevelCount() const;
		virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const override;
	private:
		void Invalidate();
		void GenerateMips();
	private:
		std::string m_Path;
		uint32_t m_Width;
		uint32_t m_Height;
		TextureProperties m_Properties;

		Buffer m_ImageData;

		Ref<Image2D> m_Image;

		ImageFormat m_Format = ImageFormat::None;
	};
	////////////////////////////////////////////////
	////// TextureCube
	////////////////////////////////////////////////
	class TextureCube : public Texture
	{
	public:
		TextureCube(ImageFormat format, uint32_t width, uint32_t height, TextureProperties properties = TextureProperties(), const void* data = nullptr);
		virtual ~TextureCube();

		void Release();
		
		void GenerateMips(bool readonly);
		VkImageView CreateImageViewSingleMip(uint32_t mip);

		virtual ImageFormat GetFormat() const override { return m_Format; }

		virtual uint32_t GetWidth() const override { return m_Width; }
		virtual uint32_t GetHeight() const override { return m_Height; };
		glm::ivec2 GetSize() const { return { m_Width, m_Height }; }
		
		virtual uint32_t GetMipLevelCount() const override { return m_MipsLevels; };
		virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const override;
		virtual uint64_t GetHash() const override { return (uint64_t)m_Info.ImageView; }


		const VkDescriptorImageInfo& GetVulkanDescriptorInfo() const { return m_DescriptorImageInfo; }
	private:
		void Invalidate();
	private:
		ImageFormat m_Format = ImageFormat::None;
		uint32_t m_Width = 0, m_Height = 0;
		TextureProperties m_Properties;
		uint32_t m_MipsLevels;

		bool m_MipsGenerated = false;
		// 6 sides of the cube
		const uint32_t m_LayersCount = 6; 
		Buffer m_ImageData;
		ImageInfo m_Info;
		VkDescriptorImageInfo m_DescriptorImageInfo = {};
	};
	////////////////////////////////////////////////
	////// TextureFactory
	////////////////////////////////////////////////
	class TextureFactory
	{
	public:
		static Ref<Texture2D> CreateBRDF();
		// color - each component in a range between 0 to 255
		static Ref<Texture2D> CreateTexture2DWithColor(const glm::ivec4& color);
		// color - each component in a range between 0 to 255
		static Ref<TextureCube> CreateTextureCubeWithColor(const glm::vec4& color);
	};
}