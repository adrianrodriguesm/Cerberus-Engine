#pragma once
#include "Renderer/RHI/Image.h"
namespace cerberus
{

	namespace utils
	{
		inline bool IsDepthFormat(ImageFormat format)
		{
			if (format == ImageFormat::DEPTH24STENCIL8 || format == ImageFormat::DEPTH32F)
				return true;

			return false;
		}
	}
	enum class BlendMode : uint8_t
	{
		None = 0,
		OneZero,
		SrcAlphaOneMinusSrcAlpha,	// Swapchain blend mode too
		Zero_SrcColor
	};

	enum class LOAD_OP : uint8_t
	{
		CLEAR,
		DONT_CARE,
		LOAD,
		NONE
	};
	enum class STORE_OP : uint8_t
	{
		DONT_CARE,
		STORE,
		NONE
	};
	struct AttachmentInfo
	{
		std::string DebugName;
		ImageFormat Format = ImageFormat::RGBA;
		LOAD_OP LoadOP;
		STORE_OP StoreOP;
		BlendMode BlendMode = BlendMode::SrcAlphaOneMinusSrcAlpha;
		bool BlendEnabled = true;
		bool IsRenderTarget = true;
	};
	struct RenderPassSpecification
	{
		std::string DebugName;
		bool Blend = true;
		std::vector<AttachmentInfo> Attachments;
	};

	class RenderPass final
	{
	public:
		RenderPass(const RenderPassSpecification& spec);
		RenderPass(RenderPass&& other) noexcept;
		RenderPass(const RenderPass&) = delete;
		RenderPass& operator=(const RenderPass&) = delete;
		RenderPass& operator=(RenderPass&&) = delete;
		~RenderPass();

		void Invalidate();

		RenderPassSpecification& GetSpecification() { return m_Specification; };
		const RenderPassSpecification& GetSpecification() const { return m_Specification; }

		const VkRenderPass GetHandle() const { return m_RenderPass; }
	private:
		RenderPassSpecification m_Specification;
		VkRenderPass m_RenderPass;

	};
}


