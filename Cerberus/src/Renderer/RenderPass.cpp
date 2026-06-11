#include "pch.h"
#include "RenderPass.h"
#include "Renderer/RendererContext.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/ResourceManager.h"
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

		inline VkAttachmentLoadOp VulkanLoadOp(LOAD_OP LoadOP)
		{
			switch (LoadOP)
			{
			case LOAD_OP::CLEAR:		return VK_ATTACHMENT_LOAD_OP_CLEAR;
			case LOAD_OP::LOAD:			return VK_ATTACHMENT_LOAD_OP_LOAD;
			case LOAD_OP::DONT_CARE:	return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			case LOAD_OP::NONE:			return VK_ATTACHMENT_LOAD_OP_NONE_EXT;
			}
			CORE_ASSERT(false, "Load operation does not exits!");
			return VK_ATTACHMENT_LOAD_OP_NONE_EXT;
		}

		inline bool ClearAttachmentOnLoad(LOAD_OP LoadOP)
		{
			return LoadOP == LOAD_OP::CLEAR;
		}

		inline VkAttachmentStoreOp VulkanStoreOp(STORE_OP StoreOP)
		{
			switch (StoreOP)
			{
			case STORE_OP::STORE:		return VK_ATTACHMENT_STORE_OP_STORE;
			case STORE_OP::DONT_CARE:	return VK_ATTACHMENT_STORE_OP_DONT_CARE;
			case STORE_OP::NONE:		return VK_ATTACHMENT_STORE_OP_NONE;
			}
			CORE_ASSERT(false, "Store operation does not exits!");
			return VK_ATTACHMENT_STORE_OP_NONE;
		}

	}

	RenderPass::RenderPass(const RenderPassSpecification& spec)
		: m_Specification(spec)
	{
		Invalidate();
	}
	RenderPass::RenderPass(RenderPass&& other) noexcept
		: m_Specification(std::move(other.m_Specification))
		, m_RenderPass(other.m_RenderPass)
	{
		other.m_RenderPass = VK_NULL_HANDLE;
	}
	RenderPass::~RenderPass()
	{
		RenderQueueManager::SubmitResourceFree([renderPass = m_RenderPass]()
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			vkDestroyRenderPass(vulkanDevice, renderPass, nullptr);
		});
	}

	void RenderPass::Invalidate()
	{
		///////////////////////////////////////////////
		///// Create Attachments and Reference
		///////////////////////////////////////////////
		std::vector<VkAttachmentDescription> attachmentDescriptions;

		std::vector<VkAttachmentReference> colorAttachmentReferences;
		VkAttachmentReference depthAttachmentReference;

		uint32_t attachmentIndex = 0;
		bool sampleDepthAttachment = false;
		for (auto& attachmentInfo : m_Specification.Attachments)
		{
			VkAttachmentDescription& attachmentDescription = attachmentDescriptions.emplace_back();
			attachmentDescription.flags = 0;
			attachmentDescription.format = utils::VulkanImageFormat(attachmentInfo.Format);
			attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescription.loadOp = utils::VulkanLoadOp(attachmentInfo.LoadOP);
			attachmentDescription.storeOp = utils::VulkanStoreOp(attachmentInfo.StoreOP);
			attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescription.initialLayout = utils::ClearAttachmentOnLoad(attachmentInfo.LoadOP) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

			///////////////////////////////////////////////////
			//// Depth Attachment and Reference 
			////////////////////////////////////////////////////
			if (utils::IsDepthFormat(attachmentInfo.Format))
			{
				sampleDepthAttachment = attachmentInfo.IsRenderTarget;
				// TODO: Separate layouts requires a "separate layouts" flag to be enabled
				if (attachmentInfo.Format == ImageFormat::DEPTH24STENCIL8 || true)
				{
					if (attachmentInfo.IsRenderTarget)

						attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
					else
						attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

					depthAttachmentReference = { attachmentIndex, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
				}
				else
				{
					if (attachmentInfo.IsRenderTarget)
						attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
					else
						attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

					depthAttachmentReference = { attachmentIndex, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL };
				}
				
			}
			////////////////////////////////////////////////////
			//// Color Attachment and Reference 
			////////////////////////////////////////////////////
			else
			{
				if (attachmentInfo.IsRenderTarget)
					attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				else
					attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
		if (sampleDepthAttachment)
			subpassDescription.pDepthStencilAttachment = &depthAttachmentReference;

		// Use subpass dependencies for layout transitions
		std::vector<VkSubpassDependency> dependencies;
		if (colorAttachmentReferences.size())
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
		if (sampleDepthAttachment)
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
	}
}
