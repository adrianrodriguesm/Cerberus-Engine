#include "pch.h"
#include "Framebuffer.h"
#include "Renderer/Core/Renderer.h"
#include "Renderer/Core/RendererContext.h"
#include "Core/Application.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Renderer/Memory/DeferredResourceDeletionQueue.h"

namespace cerberus
{
	namespace utils
	{
		inline VkFormat VulkanImageFormat(ImageFormat format)
		{
			switch (format)
			{
			case ImageFormat::RED32F:          return VK_FORMAT_R32_SFLOAT;
			case ImageFormat::RG16F:		   return VK_FORMAT_R16G16_SFLOAT;
			case ImageFormat::RG32F:		   return VK_FORMAT_R32G32_SFLOAT;
			case ImageFormat::RGBA:            return VK_FORMAT_R8G8B8A8_UNORM;
			case ImageFormat::RGBA16F:         return VK_FORMAT_R16G16B16A16_SFLOAT;
			case ImageFormat::RGBA32F:         return VK_FORMAT_R32G32B32A32_SFLOAT;
			case ImageFormat::DEPTH32F:        return VK_FORMAT_D32_SFLOAT;
			case ImageFormat::DEPTH24STENCIL8: return RendererContext::GetCurrentDevice()->GetPhysicalDevice()->GetDepthFormat();
			}
			CORE_ASSERT(false, "Image format does not exits!");
			return VK_FORMAT_UNDEFINED;
		}

	}
	Framebuffer::Framebuffer(const FramebufferSpecification& spec)
		: m_Specification(spec)
	{
		CORE_ASSERT(m_Specification.Width != 0 && m_Specification.Height != 0, 
					"Invalid framebuffer dimension! Width or Hight equal to 0");

		m_Width = m_Specification.Width;
		m_Height = m_Specification.Height;

		CORE_ASSERT(m_Specification.RenderTargetsInfo.size(), "Zero attachments specified!");

		m_Width *= (uint32_t)m_Specification.Scale;
		m_Height *= (uint32_t)m_Specification.Scale;

		Invalidate();
	}
	Framebuffer::Framebuffer(Framebuffer&& other) noexcept
		: m_Specification(std::move(other.m_Specification))
		, m_Width(other.m_Width)
		, m_Height(other.m_Height)
		, m_RenderTargets(std::move(other.m_RenderTargets))
		, m_DepthRenderTargets(other.m_DepthRenderTargets)
		, m_ClearValues(std::move(other.m_ClearValues))
		, m_Framebuffer(other.m_Framebuffer)
	{
		other.m_DepthRenderTargets = Handle<RenderTargetTexture>{};
		other.m_Framebuffer = nullptr;
	}
	Framebuffer::~Framebuffer()
	{
		// RenderTargets
		for (Handle<RenderTargetTexture> renderTarget : m_RenderTargets)
		{
			ResourceManager::DestroyRenderTarget(renderTarget);
		}
		ResourceManager::DestroyRenderTarget(m_DepthRenderTargets);
		// Framebuffer
		VkFramebuffer framebuffer = m_Framebuffer;
		DeferredResourceDeletionQueue::SubmitCurrentFrame([framebuffer]()
		{
			VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		});
	}

	void Framebuffer::Resize(uint32_t width, uint32_t height, bool forceRecreate)
	{
		if ((!forceRecreate && (m_Width == width && m_Height == height)) || !m_Specification.IsResizable)
			return;

		m_Width = width * (uint32_t)m_Specification.Scale;
		m_Height = height * (uint32_t)m_Specification.Scale;

		Invalidate();
	}

	void Framebuffer::Invalidate()
	{
		RenderPass* renderPass = ResourceManager::GetRenderPass(m_Specification.RenderPass);
		CORE_ASSERT(renderPass, "Invalid render pass attach to the framebuffer!");
		NV_CORE_TRACE("Framebuffer::Invalidate ({0})", m_Specification.DebugName);

		// In case of having a framebuffer then invalidate
		if (m_Framebuffer)
		{
			VkFramebuffer framebuffer = m_Framebuffer;
			DeferredResourceDeletionQueue::SubmitCurrentFrame([framebuffer]()
			{
				VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
				vkDestroyFramebuffer(device, framebuffer, nullptr);
			});
		}

		bool bHasDepthTarget = false;
		// Invalidate Images in case of resizing
		if (m_RenderTargets.size() || m_DepthRenderTargets.IsValid())
		{
			// Color Targets
			for (Handle<RenderTargetTexture> rtHandle : m_RenderTargets)
			{
				RenderTargetTexture* renderTarget = ResourceManager::GetRenderTarget(rtHandle);
				renderTarget->Resize(m_Width, m_Height);
			}
			// Depth target
			RenderTargetTexture* depthTarget = ResourceManager::GetRenderTarget(m_DepthRenderTargets);
			if (depthTarget)
			{
				depthTarget->Resize(m_Width, m_Height);
				bHasDepthTarget = true;
			}
		}
		else
		{
			
			m_ClearValues.resize(m_Specification.RenderTargetsInfo.size());
			uint32_t attachmentIndex = 0;
			for (const RenderTargetProperties& rtProp : m_Specification.RenderTargetsInfo)
			{
				Handle<RenderTargetTexture> rtHandle = ResourceManager::CreateRenderTarget(rtProp);
				RenderTargetTexture* renderTarget = ResourceManager::GetRenderTarget(rtHandle);

				if (renderTarget->IsDepthTarget())
				{
					bHasDepthTarget = true;
					m_DepthRenderTargets = rtHandle;
					m_ClearValues[attachmentIndex].depthStencil = { 1.0f, 0 };
				}
				else
				{
					m_RenderTargets.push_back(rtHandle);
					glm::vec4 clearColor = renderTarget->GetClearColor();
					m_ClearValues[attachmentIndex].color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
				}
				attachmentIndex++;
			}

		}
		///////////////////////////////////////////////////////////
		// Create the actual Framebuffer
		///////////////////////////////////////////////////////////
		uint32_t attachmentsSize = (uint32_t)m_Specification.RenderTargetsInfo.size();
		std::vector<VkImageView> attachments;
		attachments.reserve(attachmentsSize);
		for(const auto& rtHandle : m_RenderTargets)
		{
			RenderTargetTexture* renderTarget = ResourceManager::GetRenderTarget(rtHandle);
			Handle<ImageView> viewHandle = renderTarget->GetImageView();
			ImageView* imgView = ResourceManager::GetImageView(viewHandle);
			CORE_ASSERT(imgView, "Null image view!");
			attachments.push_back(imgView->GetHandle());
		}

		if (bHasDepthTarget)
		{
			RenderTargetTexture* renderTarget = ResourceManager::GetRenderTarget(m_DepthRenderTargets);
			Handle<ImageView> viewHandle = renderTarget->GetImageView();
			ImageView* imgView = ResourceManager::GetImageView(viewHandle);
			CORE_ASSERT(imgView, "Null image view!");
			attachments.push_back(imgView->GetHandle());
		}
		VkFramebufferCreateInfo framebufferCreateInfo = {};
		framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferCreateInfo.renderPass = renderPass->GetHandle();
		framebufferCreateInfo.attachmentCount = uint32_t(attachments.size());
		framebufferCreateInfo.pAttachments = attachments.data();
		framebufferCreateInfo.width = m_Width;
		framebufferCreateInfo.height = m_Height;
		framebufferCreateInfo.layers = 1;

		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		VK_CHECK_RESULT(vkCreateFramebuffer(vulkanDevice, &framebufferCreateInfo, nullptr, &m_Framebuffer));
	}
}