#include "pch.h"
#include "Pipeline.h"
#include "Core/Window.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/VulkanExtensions.h"
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

	GraphicsPipeline::GraphicsPipeline(PipelineSpecification spec)
		: m_Specification(spec)
	{
		Invalidate();
	}

	GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& other) noexcept
		: m_Specification(std::move(other.m_Specification))
		, m_PipelineLayout(other.m_PipelineLayout)
		, m_PipelineCache(other.m_PipelineCache)
		, m_Pipeline(other.m_Pipeline)
	{
		other.m_Pipeline = nullptr;
		other.m_PipelineLayout = nullptr;
		other.m_PipelineCache = nullptr;
	}

	GraphicsPipeline::~GraphicsPipeline()
	{
		VkPipeline pipeline = m_Pipeline;
		VkPipelineLayout pipelineLayout = m_PipelineLayout;
		VkPipelineCache pipelineCache = m_PipelineCache;

		m_Pipeline = VK_NULL_HANDLE;
		m_PipelineLayout = VK_NULL_HANDLE;
		m_PipelineCache = VK_NULL_HANDLE;

		if (!pipeline && !pipelineLayout && !pipelineCache)
			return;

		RenderQueueManager::SubmitResourceFree([pipeline, pipelineLayout, pipelineCache]() mutable
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

			if (pipeline)
				vkDestroyPipeline(vulkanDevice, pipeline, nullptr);
			if (pipelineCache)
				vkDestroyPipelineCache(vulkanDevice, pipelineCache, nullptr);
			if (pipelineLayout)
				vkDestroyPipelineLayout(vulkanDevice, pipelineLayout, nullptr);
		});
	}

	void GraphicsPipeline::Invalidate()
	{
		if (m_Pipeline || m_PipelineCache || m_PipelineLayout)
		{
			VkPipeline pipeline = m_Pipeline;
			VkPipelineCache pipelineCache = m_PipelineCache;
			VkPipelineLayout pipelineLayout = m_PipelineLayout;

			m_Pipeline = VK_NULL_HANDLE;
			m_PipelineCache = VK_NULL_HANDLE;
			m_PipelineLayout = VK_NULL_HANDLE;

			RenderQueueManager::SubmitResourceFree([pipeline, pipelineCache, pipelineLayout]() mutable
			{
				VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
				if (pipeline)
					vkDestroyPipeline(vulkanDevice, pipeline, nullptr);
				if (pipelineCache)
					vkDestroyPipelineCache(vulkanDevice, pipelineCache, nullptr);
				if (pipelineLayout)
					vkDestroyPipelineLayout(vulkanDevice, pipelineLayout, nullptr);
			});
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

		// -- VIEWPORT & SCISSOR --
		// Create a viewport info struct (viewport is how we transform something to the screen)
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
		RenderPass* renderPass = ResourceManager::GetRenderPass(m_Specification.RenderPass);
		// Blend Attachment State (How blending is handled)
		const auto& renderTargetInfo = renderPass->GetSpecification().Attachments;
		std::vector<VkPipelineColorBlendAttachmentState> blendAttachmentStates;
		blendAttachmentStates.reserve(renderTargetInfo.size());
		// Only use one attachment
		if (renderPass->GetSpecification().Blend)
		{
			for (size_t i = 0; i < renderTargetInfo.size(); i++)
			{
				
				const auto& renderTarget = renderTargetInfo[i];
				if (utils::IsDepthFormat(renderTarget.Format))
					continue;

				VkPipelineColorBlendAttachmentState& blendState = blendAttachmentStates.emplace_back();
				BlendMode blendMode = renderTarget.BlendMode;
				blendState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT		// Colors to apply blending to (in this case apply to all the colors)
					| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				blendState.blendEnable = renderTarget.BlendEnabled ? VK_TRUE : VK_FALSE;
				// Choose the src and dst blend factor
				switch (blendMode)
				{
				case BlendMode::OneZero:
					blendState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
					blendState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
					break;
				case BlendMode::SrcAlphaOneMinusSrcAlpha:
					// Blending uses equation: (srcColorBlendFactor * new color) colorBlendOp (dstColorBlendFactor * old color)
					blendState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					blendState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					break;
				case BlendMode::Zero_SrcColor:
					blendState.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
					blendState.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
					break;
				default:
					CORE_ASSERT(false, "Blend mode not implemented!");
					break;
				}
				blendState.colorBlendOp = VK_BLEND_OP_ADD;
				// Summarised: (VK_BLEND_FACTOR_SRC_ALPHA * new color) + (VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA * old color)
				//			   (new color alpha * new color) + ((1 - new color alpha) * old color)	
				//			   (srcBlendFactor * new color) + (dstBlendFactor * old color)	
				blendState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				blendState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				blendState.alphaBlendOp = VK_BLEND_OP_ADD;
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
		pipelineLayoutCreateInfo.pushConstantRangeCount = (uint32_t)pushConstantRanges.size();
		pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
		
		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
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
		// Comparison operation that allows an overwrite (is in front)
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
		pipelineCreateInfo.renderPass = renderPass->GetHandle();								// Render pass description the pipeline is compatible with
		pipelineCreateInfo.subpass = 0;															// Subpass of render pass to use with pipeline

		// Pipeline Derivatives : Can create multiple pipelines that derive from one another for optimization
		pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;	// Existing pipeline to derive from...
		pipelineCreateInfo.basePipelineIndex = -1;				// or index of pipeline being created to derive from (in case creating multiple at once)

		
		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(vulkanDevice, &pipelineCacheCreateInfo, nullptr, &m_PipelineCache));
		
		// Create Graphics Pipeline
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(vulkanDevice, m_PipelineCache, 1, &pipelineCreateInfo, nullptr, &m_Pipeline));
		

	}	

	RayTracingPipeline::RayTracingPipeline(const PipelineSpecification& spec)
		: m_Specification(spec)
	{
		Invalidate();
	}

	RayTracingPipeline::~RayTracingPipeline()
	{
		VkPipeline pipeline = m_rtPipeline;
		VkPipelineCache pipelineCache = m_rtPipelineCache;
		VkPipelineLayout pipelineLayout = m_rtPipelineLayout;

		m_rtPipeline = VK_NULL_HANDLE;
		m_rtPipelineCache = VK_NULL_HANDLE;
		m_rtPipelineLayout = VK_NULL_HANDLE;

		if (!pipeline && !pipelineCache && !pipelineLayout)
			return;

		RenderQueueManager::SubmitResourceFree([pipeline, pipelineCache, pipelineLayout]() mutable
		{
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			if (pipeline)
				vkDestroyPipeline(vulkanDevice, pipeline, nullptr);
			if (pipelineCache)
				vkDestroyPipelineCache(vulkanDevice, pipelineCache, nullptr);
			if (pipelineLayout)
				vkDestroyPipelineLayout(vulkanDevice, pipelineLayout, nullptr);
		});
	}



	void RayTracingPipeline::Invalidate()
	{
		if (m_rtPipeline || m_rtPipelineCache || m_rtPipelineLayout)
		{
			VkPipeline pipeline = m_rtPipeline;
			VkPipelineCache pipelineCache = m_rtPipelineCache;
			VkPipelineLayout pipelineLayout = m_rtPipelineLayout;

			m_rtPipeline = VK_NULL_HANDLE;
			m_rtPipelineCache = VK_NULL_HANDLE;
			m_rtPipelineLayout = VK_NULL_HANDLE;

			RenderQueueManager::SubmitResourceFree([pipeline, pipelineCache, pipelineLayout]() mutable
			{
				VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
				if (pipeline)
					vkDestroyPipeline(vulkanDevice, pipeline, nullptr);
				if (pipelineCache)
					vkDestroyPipelineCache(vulkanDevice, pipelineCache, nullptr);
				if (pipelineLayout)
					vkDestroyPipelineLayout(vulkanDevice, pipelineLayout, nullptr);
			});
		}



		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		// TODO: get rid of this....
		// Push constant structure for the ray tracer
		struct PushConstantRay
		{
			glm::vec4  ClearColor;
			glm::vec3  LightPosition;
			float LightIntensity;
			int   LightType;
		};

		// -- PUSH CONSTANT --
		//const std::vector<VkPushConstantRange>& pushConstantRanges = m_Specification.Shader->GetVulkanPushConstantRanges();
		// Push constant: we want to be able to update constants used by the shaders
		VkPushConstantRange pushConstant{ VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
										 0, sizeof(PushConstantRay) };


		// -- PIPELINE LAYOUT --
		auto& descriptorSetLayout = m_Specification.Shader->GetDescriptorSetLayouts();
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
		pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)descriptorSetLayout.size();
		pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayout.data();
		//pipelineLayoutCreateInfo.pushConstantRangeCount = (uint32_t)pushConstantRanges.size();
		//pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstant;


		VK_CHECK_RESULT(vkCreatePipelineLayout(vulkanDevice, &pipelineLayoutCreateInfo, nullptr, &m_rtPipelineLayout));

		std::vector<VkPipelineShaderStageCreateInfo> pipelineShaderStageInfo = m_Specification.Shader->GetPipelineShaderModules();

		// Find stage indices by matching the stage flag so the order of iteration
		// over the shader's unordered_map doesn't matter.
		m_RayGenIndex = VK_SHADER_UNUSED_KHR;
		m_MissHitIndex = VK_SHADER_UNUSED_KHR;
		m_TriangleHitIndex = VK_SHADER_UNUSED_KHR;
		for (uint32_t i = 0; i < (uint32_t)pipelineShaderStageInfo.size(); i++)
		{
			switch (pipelineShaderStageInfo[i].stage)
			{
			case VK_SHADER_STAGE_RAYGEN_BIT_KHR:      m_RayGenIndex      = i; break;
			case VK_SHADER_STAGE_MISS_BIT_KHR:         m_MissHitIndex     = i; break;
			case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:  m_TriangleHitIndex = i; break;
			default: break;
			}
		}
		CORE_ASSERT(m_RayGenIndex != VK_SHADER_UNUSED_KHR, "RT shader missing raygen stage!");
		CORE_ASSERT(m_MissHitIndex != VK_SHADER_UNUSED_KHR, "RT shader missing miss stage!");
		CORE_ASSERT(m_TriangleHitIndex != VK_SHADER_UNUSED_KHR, "RT shader missing closest-hit stage!");

		// TODO: MOVE this to Shader Land and abstracted
		// Shader groups
		VkRayTracingShaderGroupCreateInfoKHR rayGenGroupInfo = {};
		rayGenGroupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		rayGenGroupInfo.pNext = nullptr;
		rayGenGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		rayGenGroupInfo.generalShader = m_RayGenIndex;
		rayGenGroupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
		rayGenGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
		rayGenGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;

		VkRayTracingShaderGroupCreateInfoKHR missGroupInfo = {};
		missGroupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		missGroupInfo.pNext = nullptr;
		missGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		missGroupInfo.generalShader = m_MissHitIndex;
		missGroupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
		missGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
		missGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;

		VkRayTracingShaderGroupCreateInfoKHR triangleHitGroupInfo = {};
		triangleHitGroupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		triangleHitGroupInfo.pNext = nullptr;
		triangleHitGroupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		triangleHitGroupInfo.generalShader = VK_SHADER_UNUSED_KHR;
		triangleHitGroupInfo.closestHitShader = m_TriangleHitIndex;
		triangleHitGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
		triangleHitGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;

		std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups =
		{
			rayGenGroupInfo,
			missGroupInfo,
			triangleHitGroupInfo,
		};


		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(vulkanDevice, &pipelineCacheCreateInfo, nullptr, &m_rtPipelineCache));


		// Create ray tracing pipeline info
		VkRayTracingPipelineCreateInfoKHR pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
		pipelineInfo.pNext = nullptr;
		pipelineInfo.flags = 0;
		pipelineInfo.stageCount = static_cast<uint32_t>(pipelineShaderStageInfo.size());
		pipelineInfo.pStages = pipelineShaderStageInfo.data();
		pipelineInfo.groupCount = static_cast<uint32_t>(groups.size());
		pipelineInfo.pGroups = groups.data();
		pipelineInfo.maxPipelineRayRecursionDepth = 1;
		pipelineInfo.layout = m_rtPipelineLayout;
		pipelineInfo.basePipelineHandle = nullptr;
		pipelineInfo.basePipelineIndex = 0;
		pipelineInfo.maxPipelineRayRecursionDepth = 1;  // Ray depth

		// Create ray tracing pipeline
		VK_CHECK_RESULT(ext::vkCreateRayTracingPipelinesKHR(vulkanDevice, nullptr, nullptr, 1, &pipelineInfo, nullptr, &m_rtPipeline));

	}

}