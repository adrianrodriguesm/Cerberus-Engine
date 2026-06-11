#pragma once
#include "Renderer/Texture.h"
#include "Renderer/RenderPass.h"
namespace cerberus
{
	struct FramebufferSpecification
	{
		std::string DebugName;
		Handle<RenderPass> RenderPass;
		uint32_t Width, Height;
		float Scale = 1.f;
		bool IsResizable = true;
		std::vector<RenderTargetProperties> RenderTargetsInfo;
	};

	class Framebuffer final
	{
	public:
		Framebuffer(const FramebufferSpecification& spec);
		Framebuffer(Framebuffer&& other) noexcept;
		Framebuffer(const Framebuffer&) = delete;
		Framebuffer& operator=(const Framebuffer&) = delete;
		Framebuffer& operator=(Framebuffer&&) = delete;
		~Framebuffer();

		void Resize(uint32_t width, uint32_t height, bool forceRecreate = false);

		const FramebufferSpecification& GetSpecification() const { return m_Specification; }

		VkFramebuffer GetHandle() const { return m_Framebuffer; }

		Handle<RenderTargetTexture> GetColorTarget(uint32_t attachmentIndex = 0) const { CORE_ASSERT(attachmentIndex < m_RenderTargets.size(), "Invalid attachment index!"); return m_RenderTargets[attachmentIndex]; }
		Handle<RenderTargetTexture> GetDepthTarget() const { return m_DepthRenderTargets; }
		size_t GetColorAttachmentCount() const { return m_RenderTargets.size(); }
		bool HasDepthAttachment() const { return m_DepthRenderTargets.IsValid(); }

		Handle<RenderPass> GetRenderPass() const { return m_Specification.RenderPass; }

		const std::vector<VkClearValue>& GetVulkanClearValues() const { return m_ClearValues; }

		const uint32_t GetWidth() { return m_Width; }
		const uint32_t GetHeight() { return m_Height; }

		void Invalidate();

	private:
		FramebufferSpecification m_Specification;
		uint32_t m_Width = 0, m_Height = 0;

		std::vector<Handle<RenderTargetTexture>> m_RenderTargets;

		Handle<RenderTargetTexture> m_DepthRenderTargets;

		std::vector<VkClearValue> m_ClearValues;

		VkFramebuffer m_Framebuffer = nullptr;
	};

}