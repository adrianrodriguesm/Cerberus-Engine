#pragma once
#include "Renderer/Image.h"
namespace cerberus
{

	enum class FramebufferBlendMode
	{
		None = 0,
		OneZero,
		SrcAlphaOneMinusSrcAlpha,
		Zero_SrcColor
	};

	struct FramebufferTextureSpecification
	{
		FramebufferTextureSpecification() = default;
		FramebufferTextureSpecification(ImageFormat format) : Format(format) {}

		FramebufferBlendMode BlendMode = FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha;
		ImageFormat Format;
		bool Blend = true;
		// If the attachment will be sample
		bool SampleAttachment = true;
		// TODO: filtering/wrap
	};

	struct FramebufferAttachmentSpecification
	{
		FramebufferAttachmentSpecification() = default;
		FramebufferAttachmentSpecification(const std::initializer_list<FramebufferTextureSpecification>& attachments)
			: Attachments(attachments) {}

		std::vector<FramebufferTextureSpecification> Attachments;
	};
	class Framebuffer;
	struct FramebufferSpecification
	{
		float Scale = 1.0f;
		uint32_t Width = 0;
		uint32_t Height = 0;
		glm::vec4 ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		bool ClearColorOnLoad = true;
		bool ClearDepthOnLoad = true;
		FramebufferAttachmentSpecification AttachmentsSpecification;
		// TODO: Currently only support one sample
		uint32_t Samples = 1; // multisampling

		// Temp, needs scale
		bool NoResize = false;

		// Master switch (individual attachments can be disabled in FramebufferTextureSpecification)
		bool Blend = true;
		// None means use BlendMode in FramebufferTextureSpecification
		FramebufferBlendMode BlendMode = FramebufferBlendMode::None;

		// SwapchainTarget = screen buffer (i.e. no framebuffer)
		bool SwapchainTarget = false;

		// Note: these are used to attach multi-layered depth images and color image arrays
		Ref<Image2D> ExistingImage;
		std::vector<uint32_t> ExistingImageLayers;
		
		// Specify existing images to attach instead of creating
		// new images. attachment index -> image
		std::unordered_map<uint32_t, Ref<Image2D>> ExistingImages;


		
		// At the moment this will just create a new render pass
		// with an existing framebuffer
		Ref<Framebuffer> ExistingFramebuffer;

		std::string DebugName;
	};

	class Framebuffer
	{
	public:
		Framebuffer(const FramebufferSpecification& spec);
		~Framebuffer();

		void Resize(uint32_t width, uint32_t height, bool forceRecreate = false);

		const FramebufferSpecification& GetSpecification() const { return m_Specification; }

		Ref<Image2D> GetImage(uint32_t attachmentIndex = 0) const { CORE_ASSERT(attachmentIndex < m_AttachmentImages.size(), "Invalid attachment index!"); return m_AttachmentImages[attachmentIndex]; }
		Ref<Image2D> GetDepthImage() const { return m_DepthAttachmentImage; }
		size_t GetColorAttachmentCount() const { return m_Specification.SwapchainTarget ? 1 : m_AttachmentImages.size(); }
		bool HasDepthAttachment() const { return m_Specification.SwapchainTarget ? false : (bool)m_DepthAttachmentImage; }
		VkRenderPass GetVulkanRenderPass() const { return m_RenderPass; }
		VkFramebuffer GetVulkanFramebuffer() const;
		const std::vector<VkClearValue>& GetVulkanClearValues() const { return m_ClearValues; }

		const uint32_t GetWidth() { return m_Width; }
		const uint32_t GetHeight() { return m_Height; }

		void Invalidate();

	private:
		FramebufferSpecification m_Specification;
		uint32_t m_Width = 0, m_Height = 0;

		std::vector<Ref<Image2D>> m_AttachmentImages;
		Ref<Image2D> m_DepthAttachmentImage;

		std::vector<VkClearValue> m_ClearValues;

		VkRenderPass m_RenderPass = nullptr;
		VkFramebuffer m_Framebuffer = nullptr;
	};

}