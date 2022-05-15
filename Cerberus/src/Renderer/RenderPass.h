#pragma once
#include "Renderer/Framebuffer.h"
namespace cerberus
{
	struct RenderPassSpecification
	{
		Ref<Framebuffer> TargetFramebuffer;
		std::string DebugName;
	};
	class RenderPass
	{
	public:
		RenderPass(const RenderPassSpecification& spec);
		~RenderPass();

		RenderPassSpecification& GetSpecification() { return m_Specification; };
		const RenderPassSpecification& GetSpecification() const { return m_Specification; }

		const VkRenderPass GetVulkanRenderPass() const { return m_Specification.TargetFramebuffer->GetVulkanRenderPass(); }
	private:
		RenderPassSpecification m_Specification;
	};
}


