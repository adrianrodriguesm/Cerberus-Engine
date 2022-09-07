#include "pch.h"
#include "Framebuffer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RendererContext.h"
#include "Core/Application.h"
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

		inline bool IsDepthFormat(ImageFormat format)
		{
			if (format == ImageFormat::DEPTH24STENCIL8 || format == ImageFormat::DEPTH32F)
				return true;

			return false;
		}

	}
	Framebuffer::Framebuffer(const FramebufferSpecification& spec)
		: m_Specification(spec)
	{
		 
		if (m_Specification.Width == 0 || m_Specification.Height == 0)
		{
			m_Width = Application::Get().GetWindow()->GetWidth();
			m_Height = Application::Get().GetWindow()->GetHeight();
		}
		else
		{
			m_Width = m_Specification.Width;
			m_Height = m_Specification.Height;
		}

		// Create all image objects immediately so we can start referencing them
		uint32_t attachmentIndex = 0;
		if (!m_Specification.ExistingFramebuffer && !m_Specification.SwapchainTarget)
		{
			for (auto& attachmentSpec : m_Specification.AttachmentsSpecification.Attachments)
			{
				// Check if we already have an image on that attachment index
				if (m_Specification.ExistingImages.find(attachmentIndex) != m_Specification.ExistingImages.end())
				{
					if (!utils::IsDepthFormat(attachmentSpec.Format))
						m_AttachmentImages.emplace_back(); // This will be set later
				}
				// Otherwise create new attachments
				else if (utils::IsDepthFormat(attachmentSpec.Format))
				{
					// If an depth existing image already exist, the image will not be created
					if (m_Specification.ExistingImage)
						continue;

					ImageSpecification spec;
					spec.Format = attachmentSpec.Format;
					spec.Usage = ImageUsage::Attachment;
					spec.Width = m_Width * (uint32_t)m_Specification.Scale;
					spec.Height = m_Height * (uint32_t)m_Specification.Scale;
					spec.DebugName = fmt::format("{0}-DepthAttachment{1}", m_Specification.DebugName.empty() ? "Unnamed FB" : m_Specification.DebugName, attachmentIndex);
					m_DepthAttachmentImage = CreateRef<Image2D>(spec);
				}
				else
				{
					ImageSpecification spec;
					spec.Format = attachmentSpec.Format;
					spec.Usage = ImageUsage::Attachment;
					spec.Width = m_Width * (uint32_t)m_Specification.Scale;
					spec.Height = m_Height * (uint32_t)m_Specification.Scale;
					spec.DebugName = fmt::format("{0}-ColorAttachment{1}", m_Specification.DebugName.empty() ? "Unnamed FB" : m_Specification.DebugName, attachmentIndex);
					m_AttachmentImages.emplace_back(CreateRef<Image2D>(spec));
				}
				attachmentIndex++;

			}

		}

		CORE_ASSERT(m_Specification.AttachmentsSpecification.Attachments.size(), "Zero attachments specified!");

		m_Width *= (uint32_t)m_Specification.Scale;
		m_Height *= (uint32_t)m_Specification.Scale;

		if (!m_Specification.SwapchainTarget)
		{
			Framebuffer* instance = this;
			Renderer::Submit([instance]()
			{
				instance->Invalidate();
			});

		}
		else
		{
			Ref<Swapchain> swapchain = Application::Get().GetWindow()->GetSwapchain();
			m_RenderPass = swapchain->GetRenderPass();

			m_ClearValues.clear();
			m_ClearValues.emplace_back().color = { 0.0f, 0.0f, 0.0f, 1.0f };
			//m_ClearValues.emplace_back().depthStencil.depth = 1.f;
		}

	}
	Framebuffer::~Framebuffer()
	{
		if (m_Specification.SwapchainTarget)
			return;

		VkFramebuffer framebuffer = m_Framebuffer;
		VkRenderPass renderPass = m_RenderPass;
		Renderer::SubmitResourceFree([framebuffer, renderPass]()
		{
			VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			vkDestroyFramebuffer(device, framebuffer, nullptr);
			vkDestroyRenderPass(device, renderPass, nullptr);
		});
	}

	void Framebuffer::Resize(uint32_t width, uint32_t height, bool forceRecreate)
	{
		if ((!forceRecreate && (m_Width == width && m_Height == height)) || m_Specification.NoResize)
			return;

		m_Width = width * (uint32_t)m_Specification.Scale;
		m_Height = height * (uint32_t)m_Specification.Scale;

		if (!m_Specification.SwapchainTarget)
		{
			Framebuffer* instance = this;
			Renderer::Submit([instance]() 
			{
				instance->Invalidate();
			});
			
		}
		else
		{
			Ref<Swapchain> swapchain = Application::Get().GetWindow()->GetSwapchain();
			m_RenderPass = swapchain->GetRenderPass();

			m_ClearValues.clear();
			m_ClearValues.emplace_back().color = { 0.0f, 0.0f, 0.0f, 1.0f };
		}
	}
	VkFramebuffer Framebuffer::GetVulkanFramebuffer() const
	{
		if (m_Specification.SwapchainTarget)
		{
			return Application::Get().GetWindow()->GetCurrentSwapchain()->GetActiveFramebuffer();
		}
		return m_Framebuffer; 
	}
	void Framebuffer::Invalidate()
	{
		NV_CORE_TRACE("Framebuffer::Invalidate ({0})", m_Specification.DebugName);

		// In case of having a framebuffer then invalidate
		if (m_Framebuffer)
		{
			VkFramebuffer framebuffer = m_Framebuffer;
			VkRenderPass renderPass = m_RenderPass;
			Renderer::SubmitResourceFree([framebuffer, renderPass]()
			{
				VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
				vkDestroyFramebuffer(device, framebuffer, nullptr);
				vkDestroyRenderPass(device, renderPass, nullptr);
			});

			// Don't free the images if we don't own them
			if (!m_Specification.ExistingFramebuffer)
			{
				uint32_t attachmentIndex = 0;
				for (Ref<Image2D> image : m_AttachmentImages)
				{
					if (m_Specification.ExistingImages.find(attachmentIndex) != m_Specification.ExistingImages.end())
						continue;

					
					image->Release();
					attachmentIndex++;
				}

				// Do we own the depth image?
				if (m_DepthAttachmentImage)
				{
					// Check if the existing image have depth format
					if (m_Specification.ExistingImage && !utils::IsDepthFormat(m_Specification.ExistingImage->GetSpecification().Format))
						m_DepthAttachmentImage->Release();
				}
			}


		}
		///////////////////////////////////////////////
		///// Create Attachments and Reference
		///////////////////////////////////////////////
		std::vector<VkAttachmentDescription> attachmentDescriptions;

		std::vector<VkAttachmentReference> colorAttachmentReferences;
		VkAttachmentReference depthAttachmentReference;

		m_ClearValues.resize(m_Specification.AttachmentsSpecification.Attachments.size());

		
		if (m_Specification.ExistingFramebuffer)
			m_AttachmentImages.clear();

		uint32_t attachmentIndex = 0;
		bool sampleDepthAttachment = false;
		for (auto& attachmentSpec : m_Specification.AttachmentsSpecification.Attachments)
		{
			///////////////////////////////////////////////////
			//// Depth Attachment and Reference 
			////////////////////////////////////////////////////
			if (utils::IsDepthFormat(attachmentSpec.Format))
			{
				// Check if the existing image have depth formaat
				if (m_Specification.ExistingImage && utils::IsDepthFormat(m_Specification.ExistingImage->GetSpecification().Format))
				{
					m_DepthAttachmentImage = m_Specification.ExistingImage;
				}
				else if (m_Specification.ExistingFramebuffer)
				{
					m_DepthAttachmentImage = m_Specification.ExistingFramebuffer->GetDepthImage();
				}
				else if (m_Specification.ExistingImages.find(attachmentIndex) != m_Specification.ExistingImages.end())
				{
					Ref<Image2D> existingImage = m_Specification.ExistingImages.at(attachmentIndex);
					CORE_ASSERT(utils::IsDepthFormat(existingImage->GetSpecification().Format), "Trying to attach non-depth image as depth attachment");
					m_DepthAttachmentImage = existingImage;
				}
				else
				{
					ImageSpecification& spec = m_DepthAttachmentImage->GetSpecification();
					spec.Width = m_Width;
					spec.Height = m_Height;
					// Create immediately
					m_DepthAttachmentImage->Invalidate();
				}
				sampleDepthAttachment = attachmentSpec.SampleAttachment;

				VkAttachmentDescription& attachmentDescription = attachmentDescriptions.emplace_back();
				attachmentDescription.flags = 0;
				attachmentDescription.format = utils::VulkanImageFormat(attachmentSpec.Format);
				attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescription.loadOp = m_Specification.ClearDepthOnLoad ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				attachmentDescription.storeOp = attachmentSpec.SampleAttachment ?  VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescription.initialLayout = m_Specification.ClearDepthOnLoad ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
				
				// TODO: Separate layouts requires a "separate layouts" flag to be enabled
				if (attachmentSpec.Format == ImageFormat::DEPTH24STENCIL8 || true) 
				{
					if (attachmentSpec.SampleAttachment)
						attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
					else
						attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
					
					depthAttachmentReference = { attachmentIndex, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
				}
				else
				{
					if (attachmentSpec.SampleAttachment)
						attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
					else
						attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
					
					depthAttachmentReference = { attachmentIndex, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL };
				}
				m_ClearValues[attachmentIndex].depthStencil = { 1.0f, 0 };
			}
			////////////////////////////////////////////////////
			//// Color Attachment and Reference 
			////////////////////////////////////////////////////
			else
			{
				if (m_Specification.ExistingFramebuffer)
				{
					Ref<Image2D> existingImage = m_Specification.ExistingFramebuffer->GetImage(attachmentIndex);
					
				}
				else if (m_Specification.ExistingImages.find(attachmentIndex) != m_Specification.ExistingImages.end())
				{
					Ref<Image2D> existingImage = m_Specification.ExistingImages[attachmentIndex];
					CORE_ASSERT(!utils::IsDepthFormat(existingImage->GetSpecification().Format), "Trying to attach depth image as color attachment");
					m_AttachmentImages[attachmentIndex] = existingImage;
				}
				else
				{
					Ref<Image2D> image = m_AttachmentImages[attachmentIndex];
					ImageSpecification& spec = image->GetSpecification();
					spec.Width = m_Width;
					spec.Height = m_Height;
					image->Invalidate();
				}

				VkAttachmentDescription& attachmentDescription = attachmentDescriptions.emplace_back();
				attachmentDescription.flags = 0;
				attachmentDescription.format = utils::VulkanImageFormat(attachmentSpec.Format);
				attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescription.loadOp = m_Specification.ClearColorOnLoad ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				attachmentDescription.storeOp = attachmentSpec.SampleAttachment ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescription.initialLayout = m_Specification.ClearColorOnLoad ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				m_ClearValues[attachmentIndex].color = { {m_Specification.ClearColor.r, m_Specification.ClearColor.g, m_Specification.ClearColor.b, m_Specification.ClearColor.a} };
				colorAttachmentReferences.emplace_back(VkAttachmentReference{ attachmentIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			}
			attachmentIndex++;
		}
		//////////////////////////////////////////////////////////////
		///// Create subpass description and depencies
		//////////////////////////////////////////////////////////////
		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = uint32_t(colorAttachmentReferences.size());
		subpassDescription.pColorAttachments = colorAttachmentReferences.data();
		if (m_DepthAttachmentImage)
			subpassDescription.pDepthStencilAttachment = &depthAttachmentReference;

		// Use subpass dependencies for layout transitions
		std::vector<VkSubpassDependency> dependencies;
		if (m_AttachmentImages.size())
		{
			// Conversion from VK_IMAGE_LAYOUT_UNDEFINED or VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 
			// to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			{
				// Transition must happen after...
				VkSubpassDependency& depedency = dependencies.emplace_back();
				depedency.srcSubpass = VK_SUBPASS_EXTERNAL;									// Subpass index (VK_SUBPASS_EXTERNAL = Special value meaning outside of renderpass)
				depedency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;				// Pipeline stage (which pipeline stage need to happen before the transition takes places)
				depedency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;						// Stage access mask (memory access)
				// But must happen before...
				depedency.dstSubpass = 0;	// 0 is this the index of the fisrt sub pass (bassically the transition need to occur between the external and the 0 sub-pass)
				depedency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // The transition need to happen before this pipeline stage
				depedency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				// VK_DEPENDENCY_BY_REGION_BIT specifies that dependencies will be framebuffer - local.
				depedency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			}
			// Conversion from VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			{
				// Transition must happen after...
				VkSubpassDependency& depedency = dependencies.emplace_back();
				depedency.srcSubpass = 0;
				depedency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				depedency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				// But must happen before...
				depedency.dstSubpass = VK_SUBPASS_EXTERNAL;
				depedency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				depedency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				// VK_DEPENDENCY_BY_REGION_BIT specifies that dependencies will be framebuffer - local.
				depedency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			}
		}
		/**/
		if (m_DepthAttachmentImage)
		{
			// Conversion from VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL or VK_IMAGE_LAYOUT_UNDEFINED
			// to VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			{
				// Transition must happen after...
				VkSubpassDependency& depedency = dependencies.emplace_back();
				depedency.srcSubpass = VK_SUBPASS_EXTERNAL;
				depedency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				depedency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				// But must happen before...
				depedency.dstSubpass = 0;
				depedency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
				depedency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				depedency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			}

			// Conversion from VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL to VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
			//if(sampleDepthAttachment)
			{
				// Transition must happen after...
				VkSubpassDependency& depedency = dependencies.emplace_back();
				depedency.srcSubpass = 0;
				depedency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				depedency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				// But must happen before...
				depedency.dstSubpass = VK_SUBPASS_EXTERNAL;
				depedency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				depedency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				depedency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			}
		}
		/**/
		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		///////////////////////////////////////////////////////////
		// Create the actual RenderPass
		///////////////////////////////////////////////////////////
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
		renderPassInfo.pAttachments = attachmentDescriptions.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();
		VK_CHECK_RESULT(vkCreateRenderPass(vulkanDevice, &renderPassInfo, nullptr, &m_RenderPass));
		///////////////////////////////////////////////////////////
		// Create the actual Framebuffer
		///////////////////////////////////////////////////////////
		uint32_t attachmentsSize = (uint32_t)m_AttachmentImages.size();
		std::vector<VkImageView> attachments(attachmentsSize);
		for (uint32_t i = 0; i < m_AttachmentImages.size(); i++)
		{
			attachments[i] = m_AttachmentImages[i]->GetImageInfo().ImageView;
			CORE_ASSERT(attachments[i], "Null image view!");
		}

		if (m_DepthAttachmentImage)
		{
			if (m_Specification.ExistingImage)
			{
				CORE_ASSERT(m_Specification.ExistingImageLayers.size() == 1, "Depth attachments do not support deinterleaving");
				attachments.emplace_back(m_DepthAttachmentImage->GetLayerImageView(m_Specification.ExistingImageLayers[0]));
				CORE_ASSERT(attachments.back(), "");
			}
			else
			{
				attachments.emplace_back(m_DepthAttachmentImage->GetImageInfo().ImageView);
				CORE_ASSERT(attachments.back(), "Null image view!");
			}
		}
		VkFramebufferCreateInfo framebufferCreateInfo = {};
		framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferCreateInfo.renderPass = m_RenderPass;
		framebufferCreateInfo.attachmentCount = uint32_t(attachments.size());
		framebufferCreateInfo.pAttachments = attachments.data();
		framebufferCreateInfo.width = m_Width;
		framebufferCreateInfo.height = m_Height;
		framebufferCreateInfo.layers = 1;

		VK_CHECK_RESULT(vkCreateFramebuffer(vulkanDevice, &framebufferCreateInfo, nullptr, &m_Framebuffer));
	}
}