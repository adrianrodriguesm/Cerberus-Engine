#pragma once
#include "Core/Buffer.h"
#include "Renderer/ResourceAllocator.h"
namespace cerberus
{
	enum class ImageUsage
	{
		None = 0,
		Sampler,
		Attachment,
		Storage
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

	struct ImageSpecification
	{
		ImageFormat Format = ImageFormat::RGBA;
		ImageUsage Usage = ImageUsage::Sampler;
		uint32_t Width = 1;
		uint32_t Height = 1;
		uint32_t Mips = 1;
		uint32_t Layers = 1;
		std::string DebugName;
	};

	class Image final
	{
	public:
		Image(const ImageSpecification& specification);
		~Image() {}

		void Invalidate();
		void Release();

		uint32_t GetWidth() const { return m_Specification.Width; }
		uint32_t GetHeight() const { return m_Specification.Height; }
		float GetAspectRatio() const { return (float)m_Specification.Width / (float)m_Specification.Height; }


		ImageSpecification& GetSpecification() { return m_Specification; }
		const ImageSpecification& GetSpecification() const { return m_Specification; }
		
		const VkImageLayout GetImageLayout() const { return m_CurrentImgLayout; }

		void SetLayout(VkCommandBuffer cmd, VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask);
		void SetLayout(VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask);

		VkImage GetHandle() const { return m_Image; }

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
	private:
		ImageSpecification m_Specification;
		VkImage m_Image;
		VmaAllocation m_MemoryAlloc = nullptr;
		VkImageLayout m_CurrentImgLayout;
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

	struct SamplerSpecification
	{
		SamplerWrap SamplerWrap = SamplerWrap::Repeat;
		SamplerFilter SamplerFilter = SamplerFilter::Linear;
		bool Anisotropy = false;
		float MaxLOD, MinLOD;
	};

	struct ImageViewProperties
	{
		Handle<Image> Image;
		uint32_t BaseMip, MipCount;
		uint32_t BaseLayer, LayerCount;
		SamplerSpecification* SamplerSpec;
	};

	class ImageView final
	{
	public:
		ImageView(const ImageViewProperties& imgViewProp);
		~ImageView();

		void Release();
		void Invalidate();
		void UpdateDescriptor();

		const VkDescriptorImageInfo& GetImageDescriptorInfo() { return m_DescriptorImageInfo; }
		uint64_t GetHash() const { return (uint64_t)m_ImageView; }

		VkImageView GetHandle() const { return m_ImageView; }

	private:
		Handle<Image> m_Image;
		ImageViewProperties m_Properties;

		VkImageView m_ImageView;
		VkSampler m_Sampler;
		VkDescriptorImageInfo m_DescriptorImageInfo = {};
	};

	namespace utils
	{
		inline SamplerWrap SamplerWrapFromString(const std::string& samplerWrap)
		{
			if (samplerWrap == "Clamp")        return SamplerWrap::Clamp;
			if (samplerWrap == "Repeat")       return SamplerWrap::Repeat;

			CORE_ASSERT(false, "Unknown Sampler Wrap Type");
			return SamplerWrap::None;
		}

		inline const char* SamplerWrapToString(SamplerWrap samplerWrap)
		{
			switch (samplerWrap)
			{
			case SamplerWrap::Clamp:       return "Clamp";
			case SamplerWrap::Repeat:       return "Repeat";
			}

			CORE_ASSERT(false, "Unknown Sampler Wrap Type");
			return "None";
		}

		inline SamplerFilter SamplerFilterFromString(const std::string& samplerFilter)
		{
			if (samplerFilter == "Linear")        return SamplerFilter::Linear;
			if (samplerFilter == "Nearest")       return SamplerFilter::Nearest;

			CORE_ASSERT(false, "Unknown Sampler Filter Type");
			return SamplerFilter::None;
		}

		inline const char* SamplerFilterToString(SamplerFilter samplerFilter)
		{
			switch (samplerFilter)
			{
			case SamplerFilter::Linear:       return "Linear";
			case SamplerFilter::Nearest:      return "Nearest";
			}

			CORE_ASSERT(false, "Unknown Sampler Filter Type");
			return "None";
		}
	}
}