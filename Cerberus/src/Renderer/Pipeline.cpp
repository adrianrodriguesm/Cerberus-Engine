#include "pch.h"
#include "Pipeline.h"
#include "Renderer/Renderer.h"
#include "Core/Window.h"
namespace cerberus
{
	namespace utils
	{
		static VkCullModeFlagBits GetVulkanCullMode(CullMode cullMode)
		{
			switch (cullMode)
			{
			case CullMode::Back:			return VK_CULL_MODE_BACK_BIT;
			case CullMode::Front:			return VK_CULL_MODE_FRONT_BIT;
			case CullMode::None:			return VK_CULL_MODE_NONE;
			}

			CORE_ASSERT(false, "Unknown toplogy");
			return VK_CULL_MODE_NONE;
		}

		static VkPrimitiveTopology GetVulkanTopology(PrimitiveTopology topology)
		{
			switch (topology)
			{
			case PrimitiveTopology::Points:			return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			case PrimitiveTopology::Lines:			return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case PrimitiveTopology::Triangles:		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case PrimitiveTopology::LineStrip:		return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			case PrimitiveTopology::TriangleStrip:	return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			case PrimitiveTopology::TriangleFan:	return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
			}

			CORE_ASSERT(false, "Unknown toplogy");
			return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
		}

		static VkFormat ShaderDataTypeToVulkanFormat(ShaderDataType type)
		{
			switch (type)
			{
			case ShaderDataType::Float:     return VK_FORMAT_R32_SFLOAT;
			case ShaderDataType::Float2:    return VK_FORMAT_R32G32_SFLOAT;
			case ShaderDataType::Float3:    return VK_FORMAT_R32G32B32_SFLOAT;
			case ShaderDataType::Float4:    return VK_FORMAT_R32G32B32A32_SFLOAT;
			}
			CORE_ASSERT(false, "Unknown shader data type");
			return VK_FORMAT_UNDEFINED;
		}

		static VkCompareOp GetVulkanCompareOperator(const DepthCompareOperator compareOp)
		{
			switch (compareOp)
			{
			case DepthCompareOperator::Never:			return VK_COMPARE_OP_NEVER;
			case DepthCompareOperator::NotEqual:		return VK_COMPARE_OP_NOT_EQUAL;
			case DepthCompareOperator::Less:			return VK_COMPARE_OP_LESS;
			case DepthCompareOperator::LessOrEqual:		return VK_COMPARE_OP_LESS_OR_EQUAL;
			case DepthCompareOperator::Greater:			return VK_COMPARE_OP_GREATER;
			case DepthCompareOperator::GreaterOrEqual:	return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case DepthCompareOperator::Equal:			return VK_COMPARE_OP_EQUAL;
			case DepthCompareOperator::Always:			return VK_COMPARE_OP_ALWAYS;
			}
			CORE_ASSERT(false, "Unknown Operator");
			return VK_COMPARE_OP_MAX_ENUM;
		}
	}

	// TODO: Move to shader class
	static VkShaderModule CreateShaderModule(const std::vector<char>& code)
	{
		auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		//////////////////////////////////////////////////////////////////////////////////////
		// Shader Module Creation Info
		//////////////////////////////////////////////////////////////////////////////////////
		VkShaderModuleCreateInfo shaderModuleCreationInfo = {};
		shaderModuleCreationInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		shaderModuleCreationInfo.codeSize = code.size();									// Size of code
		shaderModuleCreationInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());	// Pointer to code (of uint32_t pointer type) ->reinterpret is using to cast pointers
		VkShaderModule shaderModule;
		VK_CHECK_RESULT(vkCreateShaderModule(vulkanDevice, &shaderModuleCreationInfo, nullptr, &shaderModule));
		return shaderModule;
	}

	static std::vector<char> LoadFile(const std::string& filename)
	{
		// Open string from given file
		// std::ios::bynary tells stream to read file as binary
		// std::ios::ate tells stream to start reading from end of file
		std::ifstream file(filename, std::ios::binary | std::ios::ate);

		ASSERT(file.is_open(), "Failed to open a file!");

		// Get current read position and use to resize file buffer
		size_t fileSize = (size_t)file.tellg();
		std::vector<char> fileBuffer(fileSize);

		// Move read position (seek to) the start of the file
		file.seekg(0);

		// Read the file data into the buffer (stream "filesize" in total)
		file.read(fileBuffer.data(), fileSize);

		file.close();

		return fileBuffer;
	}

	Pipeline::Pipeline(PipelineSpecification spec)
		: m_Specification(spec)
	{
		Pipeline* instance = this;
		Renderer::Submit([instance]()
		{
			instance->Invalidate();
		});
		
	}

	Pipeline::~Pipeline()
	{
		VkPipeline pipeline = m_VulkanPipeline;
		VkPipelineLayout pipelineLayout = m_PipelineLayout;
		VkPipelineCache pipelineCache = m_PipelineCache;
		Renderer::SubmitResourceFree([pipeline, pipelineLayout, pipelineCache]() mutable
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

			vkDestroyPipeline(vulkanDevice, pipeline, nullptr);
			vkDestroyPipelineCache(vulkanDevice, pipelineCache, nullptr);
			vkDestroyPipelineLayout(vulkanDevice, pipelineLayout, nullptr);
		});
	}

	void Pipeline::Invalidate()
	{

		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		if (m_VulkanPipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(vulkanDevice, m_VulkanPipeline, nullptr);
			vkDestroyPipelineCache(vulkanDevice, m_PipelineCache, nullptr);
			vkDestroyPipelineLayout(vulkanDevice, m_PipelineLayout, nullptr);
		}
		// CREATE PIPELINE
		// -- VERTEX INPUT
		// How the data for a single vertex (including info such as position, colour, texture coords, normals, etc) is as a whole
		VkVertexInputBindingDescription bindingDescription = {};
		bindingDescription.binding = 0;									// Can bind multiple streams of data, this defines which one
		bindingDescription.stride = m_Specification.Layout.GetStride();	// Size of a single vertex object
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;		// How to move between data after each vertex.
																		// VK_VERTEX_INPUT_RATE_VERTEX		: Move on to the next vertex
																		// VK_VERTEX_INPUT_RATE_INSTANCE	: Move to a vertex for the next instance
		// How the data for an attribute is defined within a vertex
		std::vector<VkVertexInputAttributeDescription> vertexInputAttributs(m_Specification.Layout.GetElements().size());
		VertexBufferLayout& vertexLayout = m_Specification.Layout;
		uint32_t location = 0;
		for (auto element : vertexLayout)
		{
			// Which binding the data is at (should be same as above)
			vertexInputAttributs[location].binding = 0;
			// Location in shader where data will be read from
			vertexInputAttributs[location].location = location;
			// Format the data will take (also helps define size of data)
			vertexInputAttributs[location].format = utils::ShaderDataTypeToVulkanFormat(element.Type);
			// Where this attribute is defined in the data for a single vertex
			vertexInputAttributs[location].offset = element.Offset;

			location++;
		}

		VkPipelineVertexInputStateCreateInfo vertexInputCreateInfo = {};
		vertexInputCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		if (vertexInputAttributs.size())
		{
			vertexInputCreateInfo.vertexBindingDescriptionCount =  1;
			vertexInputCreateInfo.pVertexBindingDescriptions = &bindingDescription;								// List of Vertex Binding Descriptions (data spacing/stride information)
			vertexInputCreateInfo.vertexAttributeDescriptionCount = (uint32_t)(vertexInputAttributs.size());
			vertexInputCreateInfo.pVertexAttributeDescriptions = vertexInputAttributs.data();					// List of Vertex Attribute Descriptions (data format and where to bind from)
		}
		else
		{
			vertexInputCreateInfo.vertexBindingDescriptionCount = 0;
			vertexInputCreateInfo.pVertexBindingDescriptions = nullptr;								// List of Vertex Binding Descriptions (data spacing/stride information)
			vertexInputCreateInfo.vertexAttributeDescriptionCount = 0;
			vertexInputCreateInfo.pVertexAttributeDescriptions = nullptr;					// List of Vertex Attribute Descriptions (data format and where to bind from)
		}

		// -- INPUT ASSEMBLY --
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo = {};
		inputAssemblyCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyCreateInfo.topology = utils::GetVulkanTopology(m_Specification.Topology);		// Primitive type to assemble vertices (e.g triangles, lines, etc)
		inputAssemblyCreateInfo.primitiveRestartEnable = VK_FALSE;							// Allow overriding of "strip" topology to start new primitives

		/** /
		// -- VIEWPORT & SCISSOR --
		// Create a viewport info struct (viewport is how we transform something to the screen)
		VkExtent2D extent = Window::GetCurrentSwapchain()->GetSwapchainExtent();
		VkViewport viewport = {};
		viewport.x = 0.f;												// x - start coordinate
		viewport.y = 0.f;												// y - start coordinate
		viewport.width = (float)extent.width;							// width of the viewport
		viewport.height = (float)extent.height;							// height of the viewport
		viewport.minDepth = 0.f;										// min framebuffer depth
		viewport.maxDepth = 1.f;										// max framebuffer depth

		// Create a scissor info struct
		VkRect2D scissor = {};
		// Everything between offset and extent will be visible (outside of that range will be cut)
		scissor.offset = { 0 };			// Offset to the region from
		scissor.extent = extent;		// Extent to describe region to use, starting at offset
		/**/
		VkPipelineViewportStateCreateInfo pipelineViewportCreateInfo = {};
		pipelineViewportCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		pipelineViewportCreateInfo.scissorCount = 1;
		//pipelineViewportCreateInfo.pScissors = &scissor;
		pipelineViewportCreateInfo.viewportCount = 1;
		//pipelineViewportCreateInfo.pViewports = &viewport;

		// -- DYNAMIC STATE --
		// Dynamics states to enable (e.g resize of the screen)
		// --- NOTE : IF WE DECIDE TO ADD CHANGE THIS STATE DYNAMIC MAKE SURE THAT ---
		// --- THE VIEW IS ALSO GETTING DESTROY AND CREATE A NEW WITH THE NEW SIZE ---
		std::vector<VkDynamicState> dynamicStateEnables;
		dynamicStateEnables.push_back(VK_DYNAMIC_STATE_VIEWPORT);			// Dynamic Viewport: Can resize in command buffer with vkCmdSetViewport(commandbuffer, 0, 1, &viewport)
		dynamicStateEnables.push_back(VK_DYNAMIC_STATE_SCISSOR);			// Dynamic Scissor: Can resize in command buffer with vkCmdSetScisse(commandbuffer, 0, 1, &scissor)
		if (m_Specification.Topology == PrimitiveTopology::Lines || m_Specification.Topology == PrimitiveTopology::LineStrip || m_Specification.Wireframe)
			dynamicStateEnables.push_back(VK_DYNAMIC_STATE_LINE_WIDTH);

		// Dynamic State creation info
		VkPipelineDynamicStateCreateInfo dynamicStateCreationInfo = {};
		dynamicStateCreationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicStateCreationInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
		dynamicStateCreationInfo.pDynamicStates = dynamicStateEnables.data();
		/**/
		// -- RASTERIZER -- (Convert the primitive into individual fragments)
		VkPipelineRasterizationStateCreateInfo pipelineRasterizerCreateInfo = {};
		pipelineRasterizerCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		pipelineRasterizerCreateInfo.depthClampEnable = VK_FALSE;			// Change if fragments beyond ner/far plane are clipped (default) or clamped to plane
		pipelineRasterizerCreateInfo.rasterizerDiscardEnable = VK_FALSE;	// Whether to discard data and skip rasterizer. Never creaates fragments, only suitable for pipeline without framebuffer output
		pipelineRasterizerCreateInfo.polygonMode = m_Specification.Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;	// How to handle filling points between vertices (e.g Wireframe)
		pipelineRasterizerCreateInfo.lineWidth = m_Specification.LineWidth;	// How thick lines should be when drawn (needs a device feature for a size diferent than 1)
		pipelineRasterizerCreateInfo.cullMode = utils::GetVulkanCullMode(m_Specification.CullMode);	// Which face of a triangle to cull
		pipelineRasterizerCreateInfo.frontFace = VK_COORD_SYSTEM;					// Winding to determine which side is front
		pipelineRasterizerCreateInfo.depthBiasEnable = VK_FALSE;					// Wheter to add depth bias to fragments (good for stopping "shadow acne" in shadow mapping)

		// -- MULTISAMPLING -- (For resolve attachment)
		VkPipelineMultisampleStateCreateInfo multisampleCreateInfo = {};
		multisampleCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampleCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;	// Number of samples to use per fragment
		multisampleCreateInfo.pSampleMask = nullptr;
		multisampleCreateInfo.sampleShadingEnable = VK_FALSE;				// Enable multisample shading or not

		// -- BLENDING -- (Blending fragments together)
		// Blending decides how to blend a new colour being written to a fragment, with the old value
		// We need one blend attachment state per color attachment (even if blending is not used)
		Ref<Framebuffer> framebuffer = m_Specification.RenderPass->GetSpecification().TargetFramebuffer;
		size_t colorAttachmentCount = framebuffer->GetColorAttachmentCount(); // If is swapchain target will return 1
		// Blend Attachment State (How blending is handled)
		std::vector<VkPipelineColorBlendAttachmentState> blendAttachmentStates(colorAttachmentCount);
		// Only use one attachment
		if (framebuffer->GetSpecification().SwapchainTarget)
		{	
			blendAttachmentStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT		// Colors to apply blending to (in this case apply to all the colors)
				| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blendAttachmentStates[0].blendEnable = VK_TRUE;													// Enable blending

			// Blending uses equation: (srcColorBlendFactor * new color) colorBlendOp (dstColorBlendFactor * old color)
			blendAttachmentStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blendAttachmentStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendAttachmentStates[0].colorBlendOp = VK_BLEND_OP_ADD;
			// Summarised: (VK_BLEND_FACTOR_SRC_ALPHA * new color) + (VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA * old color)
			//			   (new color alpha * new color) + ((1 - new color alpha) * old color)	
			blendAttachmentStates[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachmentStates[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;	// Dst = old color
			blendAttachmentStates[0].alphaBlendOp = VK_BLEND_OP_ADD;
			// Summarised: (1 * new alpha) + (0 * old alpha) = new alpha
		}
		else if(framebuffer->GetSpecification().Blend)
		{
			for (size_t i = 0; i < colorAttachmentCount; i++)
			{
				const auto& attachmentSpec = framebuffer->GetSpecification().AttachmentsSpecification.Attachments[i];
				FramebufferBlendMode blendMode = framebuffer->GetSpecification().BlendMode == FramebufferBlendMode::None
					? attachmentSpec.BlendMode
					: framebuffer->GetSpecification().BlendMode;
				blendAttachmentStates[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT		// Colors to apply blending to (in this case apply to all the colors)
					| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				blendAttachmentStates[i].blendEnable = attachmentSpec.Blend ? VK_TRUE : VK_FALSE;
				// Choose the src and dst blend factor
				switch (blendMode)
				{
				case FramebufferBlendMode::OneZero:
					blendAttachmentStates[i].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
					blendAttachmentStates[i].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
					break;
				case FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha:
					blendAttachmentStates[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					blendAttachmentStates[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					break;
				case FramebufferBlendMode::Zero_SrcColor:
					blendAttachmentStates[i].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
					blendAttachmentStates[i].dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
					break;
				default:
					CORE_ASSERT(false, "Blend mode not implemented!");
					break;
				}
				blendAttachmentStates[i].colorBlendOp = VK_BLEND_OP_ADD;
				//			   (srcBlendFactor * new color) + (dstBlendFactor * old color)	
				blendAttachmentStates[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				blendAttachmentStates[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				blendAttachmentStates[i].alphaBlendOp = VK_BLEND_OP_ADD;
				// Summarised: (1 * new alpha) + (0 * old alpha) = new alpha
			}
		}
		VkPipelineColorBlendStateCreateInfo colorBlendingCreateInfo = {};
		colorBlendingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendingCreateInfo.logicOpEnable = VK_FALSE;			// Alternative to calculations is to use logical operations
		colorBlendingCreateInfo.attachmentCount = (uint32_t)blendAttachmentStates.size();
		colorBlendingCreateInfo.pAttachments = blendAttachmentStates.data();

		// -- PUSH CONSTANT --
		const std::vector<VkPushConstantRange>& pushConstantRanges = m_Specification.Shader->GetVulkanPushConstantRanges();

		// -- PIPELINE LAYOUT --
		auto& descriptorSetLayout = m_Specification.Shader->GetDescriptorSetLayouts();
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
		pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)descriptorSetLayout.size();
		pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayout.data();
		pipelineLayoutCreateInfo.pushConstantRangeCount = pushConstantRanges.size();
		pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
			
		// Create Pipeline Layout
		VK_CHECK_RESULT(vkCreatePipelineLayout(vulkanDevice, &pipelineLayoutCreateInfo, nullptr, &m_PipelineLayout))

		// -- DEPTH STENCIL TESTING --
		// Depth and stencil state containing depth and stencil compare and test operations
		// We only use depth tests and want depth tests and writes to be enabled and compare with less or equal
		VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
		depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		// Enable checking depth to determine fragment write (Check the depth to verify if can write on that fragment)
		depthStencilState.depthTestEnable = m_Specification.DepthTest ? VK_TRUE : VK_FALSE;
		// Enable writing to depth buffer (to replace old values)
		depthStencilState.depthWriteEnable = m_Specification.DepthWrite ? VK_TRUE : VK_FALSE;
		// Comparison operation that allows an overwirte (is in front)
		depthStencilState.depthCompareOp = utils::GetVulkanCompareOperator(m_Specification.DepthOperator); 
		depthStencilState.depthBoundsTestEnable = VK_FALSE;			    // Depth Bounds Test: Does the depth value exist between two bounds	
		depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
		depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
		depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
		depthStencilState.stencilTestEnable = VK_FALSE;					// Enable Stencil Test
		depthStencilState.front = depthStencilState.back;

		// -- GRAPHICS PIPELINE CREATION --
		VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
		pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.stageCount = 2;													// Number of shader stages
		const auto& pipelineShaderStageInfo = m_Specification.Shader->GetPipelineShaderModules();
		pipelineCreateInfo.pStages = pipelineShaderStageInfo.data();						// List of shader stages
		pipelineCreateInfo.pVertexInputState = &vertexInputCreateInfo;						// All the fixed function pipeline states
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyCreateInfo;
		pipelineCreateInfo.pViewportState = &pipelineViewportCreateInfo;
		pipelineCreateInfo.pDynamicState = &dynamicStateCreationInfo;
		pipelineCreateInfo.pRasterizationState = &pipelineRasterizerCreateInfo;
		pipelineCreateInfo.pMultisampleState = &multisampleCreateInfo;
		pipelineCreateInfo.pColorBlendState = &colorBlendingCreateInfo;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.layout = m_PipelineLayout;											// Pipeline Layout pipeline should use
		pipelineCreateInfo.renderPass = m_Specification.RenderPass->GetVulkanRenderPass();		// Render pass description the pipeline is compatible with
		pipelineCreateInfo.subpass = 0;															// Subpass of render pass to use with pipeline

		// Pipeline Derivatives : Can create multiple pipelines that derive from one another for optimisation
		pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;	// Existing pipeline to derive from...
		pipelineCreateInfo.basePipelineIndex = -1;				// or index of pipeline being created to derive from (in case creating multiple at once)

		/**/
		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(vulkanDevice, &pipelineCacheCreateInfo, nullptr, &m_PipelineCache));
		/**/
		// Create Graphics Pipeline
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(vulkanDevice, m_PipelineCache, 1, &pipelineCreateInfo, nullptr, &m_VulkanPipeline));
		

	}	
}