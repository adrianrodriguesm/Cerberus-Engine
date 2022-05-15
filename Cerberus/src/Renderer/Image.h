#pragma once
#include "Core/Buffer.h"
#include "Renderer/VulkanAllocator.h"
namespace cerberus
{
	enum class ImageUsage
	{
		None = 0,
		Texture,
		Attachment,
		Storage
	};

	enum class SamplerWrap
	{
		None = 0,
		Clamp,
		Repeat
	};

	enum class SamplerFilter
	{
		None = 0,
		Linear,
		Nearest
	};

	struct ImageInfo
	{
		VkImage Image = nullptr;
		VkImageView ImageView = nullptr;
		VkSampler Sampler = nullptr;
		VmaAllocation MemoryAlloc = nullptr;
	};

	enum class ImageFormat
	{
		None = 0,
		RED32F,
		RGB,
		RGBA,
		RGBA16F,
		RGBA32F,
		RG16F,
		RG32F,
		SRGBA,

		DEPTH32F,
		DEPTH24STENCIL8,
		// TODO: Fix!
		DEPTH32STENCIL8 = DEPTH24STENCIL8,

		// Defaults
		DEPTH = DEPTH24STENCIL8,
	};

	struct SamplerSpecification
	{
		SamplerWrap SamplerWrap = SamplerWrap::Repeat;
		SamplerFilter SamplerFilter = SamplerFilter::Linear;
		bool Anisotropy = false;
	};

	struct ImageSpecification
	{
		ImageFormat Format = ImageFormat::RGBA;
		ImageUsage Usage = ImageUsage::Texture;
		SamplerSpecification SamplerSpec;
		uint32_t Width = 1;
		uint32_t Height = 1;
		uint32_t Mips = 1;
		uint32_t Layers = 1;
		std::string DebugName;
	};

	class Image
	{
	public:
		virtual ~Image() {}

		virtual void Invalidate() = 0;
		virtual void Release() = 0;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual float GetAspectRatio() const = 0;

		virtual ImageSpecification& GetSpecification() = 0;
		virtual const ImageSpecification& GetSpecification() const = 0;

		virtual Buffer GetBuffer() const = 0;
		virtual Buffer& GetBuffer() = 0;

		static inline uint32_t GetImageFormatBPP(ImageFormat format)
		{
			switch (format)
			{
			case ImageFormat::RED32F:	return 4;
			case ImageFormat::RGB:		return 3;
			case ImageFormat::RGBA:		return 4;
			case ImageFormat::SRGBA:    return 4;
			case ImageFormat::RGBA16F:	return 2 * 4;
			case ImageFormat::RGBA32F:	return 4 * 4;
			}
			CORE_ASSERT(false, "Image Format does not exits!");
			return 0;
		}

		static inline uint32_t CalculateMipCount(uint32_t width, uint32_t height)
		{
			// The max function selects the largest dimension.
			// The log2 function calculates how many times that dimension can be divided by 2. 
			// The floor function handles cases where the largest dimension is not a power of 2. 
			// 1 is added so that the original image has a mip level.
			return (uint32_t)std::floor(std::log2(glm::max(width, height))) + 1;
		}

		static inline uint32_t GetImageMemorySize(ImageFormat format, uint32_t width, uint32_t height)
		{
			return width * height * GetImageFormatBPP(format);
		}

		static inline bool IsDepthFormat(ImageFormat format)
		{
			if (format == ImageFormat::DEPTH24STENCIL8 || format == ImageFormat::DEPTH32F)
				return true;

			return false;
		}
	};

	class Image2D : public Image
	{
	public:
		Image2D(const ImageSpecification& specification);
		virtual ~Image2D();

		virtual void Invalidate() override;
		virtual void Release() override;
		void UpdateDescriptor();
		void UpdateImageLayout();
		void CreatePerLayerImageViews();
		void CreatePerSpecificLayerImageViews(const std::vector<uint32_t>& layerIndices);
		
		VkImageView GetLayerImageView(uint32_t layer)
		{
			CORE_ASSERT(layer < m_PerLayerImageViews.size(), "Layer level does not exists!");
			return m_PerLayerImageViews[layer];
		}

		virtual uint32_t GetWidth() const override { return m_Specification.Width; }
		virtual uint32_t GetHeight() const override { return m_Specification.Height; }
		virtual float GetAspectRatio() const override { return (float)m_Specification.Width / (float)m_Specification.Height; }

		uint64_t GetHash() const { return (uint64_t)m_Info.ImageView; };

		virtual ImageSpecification& GetSpecification() override { return m_Specification; }
		virtual const ImageSpecification& GetSpecification() const override { return m_Specification; }
		
		virtual Buffer GetBuffer() const { return m_ImageData; }
		virtual Buffer& GetBuffer() override { return m_ImageData; }

		ImageInfo& GetImageInfo() { return m_Info; }
		const ImageInfo& GetImageInfo() const { return m_Info; }

		const VkDescriptorImageInfo& GetVulkanDescriptorInfo() { return m_DescriptorImageInfo; }

	private:
		ImageSpecification m_Specification;

		Buffer m_ImageData;

		ImageInfo m_Info;
		
		std::vector<VkImageView> m_PerLayerImageViews;

		VkDescriptorImageInfo m_DescriptorImageInfo = {};
	};

	namespace utils
	{
		inline SamplerWrap SamplerWrapFromString(const std::string& samplerWrap)
		{
			if (samplerWrap == "Clamp")        return SamplerWrap::Clamp;
			if (samplerWrap == "Repeat")       return SamplerWrap::Repeat;

			CORE_ASSERT(false, "Unknown Asset Type");
			return SamplerWrap::None;
		}

		inline const char* SamplerWrapToString(SamplerWrap samplerWrap)
		{
			switch (samplerWrap)
			{
			case SamplerWrap::Clamp:       return "Clamp";
			case SamplerWrap::Repeat:       return "Repeat";
			}

			CORE_ASSERT(false, "Unknown Asset Type");
			return "None";
		}

		inline SamplerFilter SamplerFilterFromString(const std::string& samplerFilter)
		{
			if (samplerFilter == "Linear")        return SamplerFilter::Linear;
			if (samplerFilter == "Nearest")       return SamplerFilter::Nearest;

			CORE_ASSERT(false, "Unknown Asset Type");
			return SamplerFilter::None;
		}

		inline const char* SamplerFilterToString(SamplerFilter samplerFilter)
		{
			switch (samplerFilter)
			{
			case SamplerFilter::Linear:       return "Linear";
			case SamplerFilter::Nearest:      return "Nearest";
			}

			CORE_ASSERT(false, "Unknown Asset Type");
			return "None";
		}
	}
}