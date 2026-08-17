#pragma once
#include "Core/Handle.h"
#include "Renderer/Image.h"
#include "Core/Buffer.h"
#include "Asset/Asset.h"
namespace cerberus
{
	class TextureAsset;

	// Texture assets store source pixels. GPU textures select how those pixels are
	// interpreted when the Vulkan image is created.
	enum class TextureColorSpace : uint8_t
	{
		AssetDefault = 0,
		Linear,
		SRGB
	};

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

	class Texture
	{
	public:
		virtual ~Texture() {}
		virtual ImageFormat GetFormat() const = 0;
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual uint32_t GetMipLevelCount() const = 0;
		virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const = 0;
		virtual uint64_t GetHash() const = 0;
	};
	////////////////////////////////////////////////
	////// Texture2D
	////////////////////////////////////////////////
	class Texture2D final : public Texture
	{
	public:
		Texture2D(ImageFormat format, const uint32_t width, const uint32_t height, const void* data, TextureProperties properties = {});
		Texture2D(const Ref<TextureAsset>& textureAsset, TextureColorSpace colorSpace = TextureColorSpace::AssetDefault);
		Texture2D(Texture2D&& other) noexcept;
		Texture2D(const Texture2D&) = delete;
		Texture2D& operator=(const Texture2D&) = delete;
		Texture2D& operator=(Texture2D&&) = delete;
		virtual ~Texture2D();


		Handle<ImageView> GetImageView() const { return m_ImageView; }
		Handle<Image> GetImage() const { return m_Image; }

		virtual ImageFormat GetFormat() const override;
		virtual uint32_t GetWidth() const override;
		virtual uint32_t GetHeight() const override;
		virtual uint64_t GetHash() const override;

		bool Loaded() const { return m_Image.IsValid() && m_ImageView.IsValid(); }

		const TextureProperties& GetTextureProperties() const { return m_Properties; }
		AssetHandle GetSourceAssetHandle() const { return m_TextureAsset; }
		uint32_t GetMipLevelCount() const;
		virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const override;
	private:
		void Initialize(ImageFormat format, uint32_t width, uint32_t height, const void* data);
		void Invalidate(ImageFormat format, const uint32_t width, const uint32_t height);
		void GenerateMips();
	private:
		TextureProperties m_Properties;
		Buffer m_ImageData;
		Handle<Image> m_Image;
		Handle<ImageView> m_ImageView;
		AssetHandle m_TextureAsset;
	};
	////////////////////////////////////////////////
	////// TextureCube
	////////////////////////////////////////////////
	class TextureCube final : public Texture
	{
	public:
		TextureCube(ImageFormat format, uint32_t width, uint32_t height, TextureProperties properties = TextureProperties(), const void* data = nullptr);
		TextureCube(TextureCube&& other) noexcept;
		TextureCube(const TextureCube&) = delete;
		TextureCube& operator=(const TextureCube&) = delete;
		TextureCube& operator=(TextureCube&&) = delete;
		virtual ~TextureCube();

		void Release();
		
		void GenerateMips(bool readonly);
		// TODO: delete this
		VkImageView CreateImageViewSingleMip(uint32_t mip);

		virtual ImageFormat GetFormat() const override { return m_Format; }

		virtual uint32_t GetWidth() const override { return m_Width; }
		virtual uint32_t GetHeight() const override { return m_Height; };
		glm::ivec2 GetSize() const { return { m_Width, m_Height }; }
		
		virtual uint32_t GetMipLevelCount() const override { return m_MipsLevels; };
		virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const override;
		virtual uint64_t GetHash() const override;


		//const VkDescriptorImageInfo& GetVulkanDescriptorInfo() const { return m_DescriptorImageInfo; }
	private:
		void Invalidate(ImageFormat format, const uint32_t width, const uint32_t height);
	private:
		ImageFormat m_Format = ImageFormat::None;
		uint32_t m_Width = 0, m_Height = 0;
		TextureProperties m_Properties;
		uint32_t m_MipsLevels;

		Handle<Image> m_Image;
		Handle<ImageView> m_ImageView;
		AssetHandle m_TextureAsset;

		// 6 sides of the cube
		Buffer m_ImageData;
	};

	struct RenderTargetProperties
	{
		std::string DebugName;
		Handle<Image> ExistingImage;

		ImageFormat Format = ImageFormat::RGBA;
		ImageViewType ViewType = ImageViewType::Image2D;

		uint32_t Width, Height;
 
		bool Resizable = true, ShaderResource = true;

		uint32_t BaseMip = 0, MipCount = 1;
		uint32_t BaseLayer = 0, LayerCount = 1;
		
		glm::vec4 ClearColor = { 0.f, 0.f, 0.f, 0.f };
	};

	class RenderTargetTexture final : public Texture
	{
	public:
		RenderTargetTexture(const RenderTargetProperties& rtProp);
		RenderTargetTexture(RenderTargetTexture&& other) noexcept;
		RenderTargetTexture(const RenderTargetTexture&) = delete;
		RenderTargetTexture& operator=(const RenderTargetTexture&) = delete;
		RenderTargetTexture& operator=(RenderTargetTexture&&) = delete;
		virtual ~RenderTargetTexture();

		void Release();
		void Resize(uint32_t width, uint32_t height);

		Handle<ImageView> GetImageView() const { return m_ImageView; }

		glm::vec4 GetClearColor() const { return m_Properties.ClearColor; }

		virtual ImageFormat GetFormat() const override { return m_Properties.Format; }

		bool IsDepthTarget() const { return Image::IsDepthFormat(m_Properties.Format); };

		virtual uint32_t GetWidth() const override { return m_Properties.Width; }
		virtual uint32_t GetHeight() const override { return m_Properties.Height; };
		glm::ivec2 GetSize() const { return { m_Properties.Width, m_Properties.Height }; }

		virtual uint32_t GetMipLevelCount() const override { return m_Properties.MipCount; };
		virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const override;
		virtual uint64_t GetHash() const override;

	private:
		void Invalidate();
	private:
		Handle<Image> m_Image;
		Handle<ImageView> m_ImageView;

		RenderTargetProperties m_Properties;
	};

	////////////////////////////////////////////////
	////// TextureFactory
	////////////////////////////////////////////////
	class TextureFactory
	{
	public:
		static Handle<Texture2D> CreateBRDF();
		// color - each component in a range between 0 to 255
		static Handle<Texture2D> CreateTexture2DWithColor(const glm::ivec4& color);
		// color - each component in a range between 0 to 255
		static Handle<TextureCube> CreateTextureCubeWithColor(const glm::vec4& color);
	};
}
