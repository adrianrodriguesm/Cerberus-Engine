#include "pch.h"
#include "Renderer.h"
#include "Core/Application.h"
#include "Renderer/ResourceAllocator.h"
#include "Renderer/UniformBufferSet.h"
#include "Renderer/ComputePipeline.h"
#include "Renderer/DescriptorManager.h"
#include "Utils/StringUtils.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/DeferredResourceDeletionQueue.h"
#include "Renderer/RendererRuntimeState.h"
#include "Renderer/RendererResources.h"
#include "Renderer/ImGuiRenderer.h"

namespace cerberus
{
	struct RendererData
	{
		RendererCommandState Commands;
		RendererDescriptorState Descriptors;
		RendererFrameState Frame;
	};

	static RendererData* s_Data;

	void Renderer::Init()
	{
		s_Data = new RendererData();

		auto vulkanDevice = RendererContext::GetCurrentDevice();
		

		s_Data->Frame.DeviceLimits = vulkanDevice->GetPhysicalDeviceLimits();

		RenderQueueManager::Init();
		DeferredResourceDeletionQueue::Init(Renderer::GetRendererConfig().FramesInFlight);
		DescriptorManager::Init();
		ResourceManager::Init();

		s_Data->Commands.GraphicsCommandPool = CreateScope<CommandPool>(CommandPoolQueueType::Graphics,
			VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			"Renderer Graphics Command Pool");
		s_Data->Commands.ComputeCommandPool = CreateScope<CommandPool>(CommandPoolQueueType::Compute,
			VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			"Renderer Compute Command Pool");
		
		RendererResources::Init();
		s_Data->Commands.MainCommandBuffer = CreateScope<CommandBuffer>(s_Data->Commands.GraphicsCommandPool.get(), ResourceManager::GetFramesInFlight(), "Main Command Buffer");
		s_Data->Commands.SwapchainCommandBuffer = CreateScope<CommandBuffer>(true, "Swapchain");
	
		// Compile shaders and upload/create default GPU resources queued by RendererResources.
		RenderQueueManager::ExecuteRenderQueue();
		ImGuiRenderer::Init(*s_Data->Commands.GraphicsCommandPool);
	}

	void Renderer::Shutdown()
	{
		auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		auto WaitAndDrain = [&]()
			{
				vkDeviceWaitIdle(vulkanDevice);
				RenderQueueManager::ExecuteRenderQueue();
				DeferredResourceDeletionQueue::FlushAll();
			};

		// ---------------------------------------------------------------------
		// 1. Make sure no GPU/render-thread work is pending before teardown.
		// ---------------------------------------------------------------------
		WaitAndDrain();

		// ---------------------------------------------------------------------
		// 2. Destroy renderer-owned ResourceManager resources.
		//    These may enqueue ResourceManager/native Vulkan frees.
		// ---------------------------------------------------------------------
		RendererResources::Shutdown();
		WaitAndDrain();

		// ---------------------------------------------------------------------
		// 3. Destroy renderer subsystems that own backend resources.
		// ---------------------------------------------------------------------
		ComputePipeline::ReleaseComputeFence();

		ImGuiRenderer::Shutdown();

		s_Data->Descriptors.RendererDescriptorSetCache.clear();

		s_Data->Commands.MainCommandBuffer.reset();
		s_Data->Commands.SwapchainCommandBuffer.reset();

		// CommandBuffer destruction can enqueue deferred frees for fences/query pools.
		WaitAndDrain();

		// ---------------------------------------------------------------------
		// 4. Destroy ResourceManager resources while device/VMA/DescriptorManager
		//    are still alive.
		// ---------------------------------------------------------------------
		ResourceManager::Shutdown();
		WaitAndDrain();

		// ---------------------------------------------------------------------
		// 5. DescriptorManager owns descriptor pools/layout cache.
		//    It must shutdown after ResourceManager released BindGroups/Layouts.
		// ---------------------------------------------------------------------
		DescriptorManager::Shutdown();
		WaitAndDrain();

		// ---------------------------------------------------------------------
		// 6. Command pools are renderer-owned execution infrastructure.
		//    Destroy after command buffers and after systems that may need transient
		//    command buffers during shutdown.
		// ---------------------------------------------------------------------
		s_Data->Commands.GraphicsCommandPool.reset();
		s_Data->Commands.ComputeCommandPool.reset();

		WaitAndDrain();

		// ---------------------------------------------------------------------
		// 7. At this point all GPU/VMA-backed resources should be gone.
		// ---------------------------------------------------------------------
		const auto& stats = ResourceAllocator::GetStats();
		NV_CORE_WARN("Memory allocated: {0}", stats.Used);
		NV_CORE_WARN("Memory free: {0}", stats.Free);

		// ---------------------------------------------------------------------
		// 8. Shutdown queue storage last. These should not execute anything.
		// ---------------------------------------------------------------------
		DeferredResourceDeletionQueue::Shutdown();
		RenderQueueManager::Shutdown();

		delete s_Data;
		s_Data = nullptr;
	}

	void Renderer::BeginFrame()
	{
		// This must happen at the frame boundary before any frame-local descriptor set is allocated
		// or command buffer is recorded. Do not enqueue it into RenderQueueManager, otherwise it may
		// execute after render passes have already allocated/bound descriptor sets.
		DescriptorManager::ResetTransientFramePools();
		s_Data->Frame.DrawCallCount = 0;
	}

	void Renderer::EndFrame()
	{
	}

	// Frame -> Set -> Descriptors
	static std::vector<std::vector<std::vector<VkWriteDescriptorSet>>>& RetrieveOrCreateUniformBufferWriteDescriptors(Ref<UniformBufferSet> uniformBufferSet, Ref<Shader> shader)
	{
		size_t shaderHash = shader->GetHash();
#if OLD_RENDERER
		// If the uniform buffer already extis in the map use that write descriptor set
		if (s_Data->Descriptors.UniformBufferWriteDescriptorCache.find(uniformBufferSet.get()) != s_Data->Descriptors.UniformBufferWriteDescriptorCache.end())
		{
			auto& shaderMap = s_Data->Descriptors.UniformBufferWriteDescriptorCache.at(uniformBufferSet.get());
			if (shaderMap.find(shaderHash) != shaderMap.end())
			{
				auto& writeDescriptors = shaderMap.at(shaderHash);
				return writeDescriptors;
			}
		}

		uint32_t framesInFlight = Renderer::GetRendererConfig().FramesInFlight;
		// Check if the Shader has descriptor sets
		if (shader->HasDescriptorSet(0))
		{
			const auto& shaderDescriptorSets = shader->GetShaderDescriptorSets();
			if (!shaderDescriptorSets.empty())
			{
				for (uint32_t i = 0; i < shaderDescriptorSets.size(); i++)
				{
					for (auto&& [binding, shaderUB] : shaderDescriptorSets[i].UniformBuffers)
					{
						// Add this new entry to the descriptor set map
						auto& writeDescriptors = s_Data->Descriptors.UniformBufferWriteDescriptorCache[uniformBufferSet.get()][shaderHash];
						writeDescriptors.resize(framesInFlight);
						for (uint32_t frame = 0; frame < framesInFlight; frame++)
						{
							Ref<UniformBuffer> uniformBuffer = uniformBufferSet->Get(binding, i, frame);
							VkWriteDescriptorSet writeDescriptorSet = {};
							writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
							writeDescriptorSet.descriptorCount = 1;
							writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
							writeDescriptorSet.pBufferInfo = &uniformBuffer->GetBufferDescriptorInfo();
							writeDescriptorSet.dstBinding = uniformBuffer->GetBindingPoint();
							writeDescriptors[frame].resize(shaderDescriptorSets.size());
							writeDescriptors[frame][i].push_back(writeDescriptorSet);
						}
					}
				}

			}
		}
#endif
		return s_Data->Descriptors.UniformBufferWriteDescriptorCache[uniformBufferSet.get()][shaderHash];
	}

	void Renderer::UpdateMaterialForRendering(Ref<Material> material, Ref<UniformBufferSet> uniformBufferSet)
	{
#if OLD_RENDERER
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		auto writeDescriptors = RetrieveOrCreateUniformBufferWriteDescriptors(uniformBufferSet, material->GetShader());
		material->UpdateForRender(writeDescriptors);
#endif
	}

	Ref<RendererContext> Renderer::GetRenderContext()
	{
		return Application::Get().GetWindow()->GetRendererContext();
	}

	void Renderer::BeginRenderPass(CommandBuffer* commandBuffer, Handle<RenderPass> hdRenderPass, const DrawArea& drawArea)
	{
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		
		Framebuffer* framebuffer = ResourceManager::GetFramebuffer(drawArea.Framebuffer);
		RenderPass* renderPass = ResourceManager::GetRenderPass(hdRenderPass);

		VkCommandBuffer vulkanCmdBuffer = commandBuffer->GetCommandBuffer(frameIndex);
		uint32_t width = framebuffer->GetWidth();
		uint32_t height = framebuffer->GetHeight();
		// Information about how to begin a render pass (only needed for graphical applications)
		VkRenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderPass->GetHandle();				// Render Pass to begin
		renderPassBeginInfo.renderArea.offset = { 0, 0 };						// Start point of render pass in pixels
		renderPassBeginInfo.renderArea.extent = { width, height };				// Size of region to run render pass on (starting at offset)


		// A clear value for each attachment
		// Each element represent an attachement of the renderpass/framebuffer
		// The order is important becuase is the same as the attachments in renderpass/framebuffer
		std::vector< VkClearValue> clearValues = framebuffer->GetVulkanClearValues();
		renderPassBeginInfo.pClearValues = clearValues.data();
		renderPassBeginInfo.clearValueCount = (uint32_t)clearValues.size();
		renderPassBeginInfo.framebuffer = framebuffer->GetHandle();
		// Begin Render Pass
		vkCmdBeginRenderPass(vulkanCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);


		if (drawArea.ExplicitClear)
		{
			const uint32_t colorAttachmentCount = (uint32_t)framebuffer->GetColorAttachmentCount();
			const uint32_t totalAttachmentCount = colorAttachmentCount + (framebuffer->HasDepthAttachment() ? 1 : 0);
			CORE_ASSERT(clearValues.size() == totalAttachmentCount, "");

			std::vector<VkClearAttachment> attachments(totalAttachmentCount);
			std::vector<VkClearRect> clearRects(totalAttachmentCount);
			for (uint32_t i = 0; i < colorAttachmentCount; i++)
			{
				attachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				attachments[i].colorAttachment = i;
				attachments[i].clearValue = clearValues[i];

				clearRects[i].rect.offset = { (int32_t)0, (int32_t)0 };
				clearRects[i].rect.extent = { width, height };
				clearRects[i].baseArrayLayer = 0;
				clearRects[i].layerCount = 1;
			}

			if (framebuffer->HasDepthAttachment())
			{
				attachments[colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				attachments[colorAttachmentCount].clearValue = clearValues[colorAttachmentCount];
				clearRects[colorAttachmentCount].rect.offset = { (int32_t)0, (int32_t)0 };
				clearRects[colorAttachmentCount].rect.extent = { width, height };
				clearRects[colorAttachmentCount].baseArrayLayer = 0;
				clearRects[colorAttachmentCount].layerCount = 1;
			}

			vkCmdClearAttachments(vulkanCmdBuffer, totalAttachmentCount, attachments.data(), totalAttachmentCount, clearRects.data());

		}

		VkViewport viewport = {};
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		viewport.x = 0.f;
		viewport.y = 0.f;
		viewport.width = (float)width;
		viewport.height = (float)height;
		// Update dynamic viewport state
		vkCmdSetViewport(vulkanCmdBuffer, 0, 1, &viewport);


		// Update dynamic scissor state
		VkRect2D scissor = {};
		scissor.extent = { width, height };
		scissor.offset = { 0, 0 };
		vkCmdSetScissor(vulkanCmdBuffer, 0, 1, &scissor);


		// Bind Descriptor Sets
		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(drawArea.GraphicsPipeline);
		CORE_ASSERT(pipeline, "Null graphics pipeline");
		// Bind Pipeline to be used in render pass
		vkCmdBindPipeline(vulkanCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());

		VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
		BindGroup* bindGroup0 = ResourceManager::GetBindGroup(drawArea.BindGroup0);
		BindGroup* bindGroup1 = ResourceManager::GetBindGroup(drawArea.BindGroup1);
		std::array<VkDescriptorSet, 2> descriptorSets =
		{
			bindGroup0->GetHandle(frameIndex),
			bindGroup1->GetHandle(frameIndex)

		};

		// Bind the descriptor set
		vkCmdBindDescriptorSets(vulkanCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, (uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
#if OLD_RENDERER
		Renderer::Submit([CommandBuffer, renderPass, explicitClear]()
		{
			Ref<Framebuffer> framebuffer = renderPass->GetSpecification().TargetFramebuffer;
			uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

			VkCommandBuffer commandBuffer = CommandBuffer->GetCommandBuffer(frameIndex);
			uint32_t width, height;
			if (framebuffer->GetSpecification().SwapchainTarget)
			{
				VkExtent2D swapchainExtend = Application::Get().GetWindow()->GetCurrentSwapchain()->GetSwapchainExtent();
				width = swapchainExtend.width;
				height = swapchainExtend.height;
			}
			else
			{
				width = framebuffer->GetWidth();
				height = framebuffer->GetHeight();
			}
			// Information about how to begin a render pass (only needed for graphical applications)
			VkRenderPassBeginInfo renderPassBeginInfo = {};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = renderPass->GetVulkanRenderPass();			// Render Pass to begin
			renderPassBeginInfo.renderArea.offset = { 0, 0 };							// Start point of render pass in pixels
			renderPassBeginInfo.renderArea.extent = { width, height };					// Size of region to run render pass on (starting at offset)


			// A clear value for each attachment
			// Each element represent an attachement of the renderpass/framebuffer
			// The order is important becuase is the same as the attachments in renderpass/framebuffer
			std::vector< VkClearValue> clearValues = framebuffer->GetVulkanClearValues();
			renderPassBeginInfo.pClearValues = clearValues.data();
			renderPassBeginInfo.clearValueCount = (uint32_t)clearValues.size();
			renderPassBeginInfo.framebuffer = framebuffer->GetVulkanFramebuffer();
			// Begin Render Pass
			vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			

			if (explicitClear)
			{
				const uint32_t colorAttachmentCount = (uint32_t)framebuffer->GetColorAttachmentCount();
				const uint32_t totalAttachmentCount = colorAttachmentCount + (framebuffer->HasDepthAttachment() ? 1 : 0);
				CORE_ASSERT(clearValues.size() == totalAttachmentCount, "");

				std::vector<VkClearAttachment> attachments(totalAttachmentCount);
				std::vector<VkClearRect> clearRects(totalAttachmentCount);
				for (uint32_t i = 0; i < colorAttachmentCount; i++)
				{
					attachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					attachments[i].colorAttachment = i;
					attachments[i].clearValue = clearValues[i];

					clearRects[i].rect.offset = { (int32_t)0, (int32_t)0 };
					clearRects[i].rect.extent = { width, height };
					clearRects[i].baseArrayLayer = 0;
					clearRects[i].layerCount = 1;
				}

				if (framebuffer->HasDepthAttachment())
				{
					attachments[colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
					attachments[colorAttachmentCount].clearValue = clearValues[colorAttachmentCount];
					clearRects[colorAttachmentCount].rect.offset = { (int32_t)0, (int32_t)0 };
					clearRects[colorAttachmentCount].rect.extent = { width, height };
					clearRects[colorAttachmentCount].baseArrayLayer = 0;
					clearRects[colorAttachmentCount].layerCount = 1;
				}

				vkCmdClearAttachments(commandBuffer, totalAttachmentCount, attachments.data(), totalAttachmentCount, clearRects.data());

			}

			VkViewport viewport = {};
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			viewport.x = 0.f;
			viewport.y = framebuffer->GetSpecification().SwapchainTarget ? 0.f : 0;
			viewport.width = (float)width;
			viewport.height = framebuffer->GetSpecification().SwapchainTarget ? (float)height : (float)height;
			// Update dynamic viewport state
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);


			// Update dynamic scissor state
			VkRect2D scissor = {};
			scissor.extent = { width, height };
			scissor.offset = { 0, 0 };
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		});
#endif

	}

	void Renderer::EndRenderPass(CommandBuffer* commandBuffer)
	{
		// End Render Pass
		vkCmdEndRenderPass(commandBuffer->GetCommandBuffer(Renderer::GetCurrentFrameIndex()));

		s_Data->Frame.DrawCallCount = 0;
#if OLD_RENDERER
		Renderer::Submit([CommandBuffer]()
		{
			// End Render Pass
			vkCmdEndRenderPass(CommandBuffer->GetCommandBuffer(Renderer::GetCurrentFrameIndex()));

			s_Data->Frame.DrawCallCount = 0;
		});
#endif
	}

	void Renderer::SetActiveVertexAndIndexBuffer(CommandBuffer* commandBuffer, const Handle<GPUBuffer> hVertexBuffer, const Handle<GPUBuffer> hInderBuffer)
	{
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		VkCommandBuffer vulkanCmdBuffer = commandBuffer->GetCommandBuffer(frameIndex);

		// Index Buffer
		GPUBuffer* indexBuffer = ResourceManager::GetBuffer(hInderBuffer);
		CORE_ASSERT(indexBuffer, "Null GPU index buffer");
		// VK_INDEX_TYPE_UINT32 : The type of data that index buffer stores
		// Bind index buffer
		vkCmdBindIndexBuffer(vulkanCmdBuffer, indexBuffer->Buffer, 0, VK_INDEX_TYPE_UINT32);
		
		// Vertex Buffer
		GPUBuffer* vertexBuffer = ResourceManager::GetBuffer(hVertexBuffer);
		CORE_ASSERT(vertexBuffer, "Null GPU vertex buffer");
		VkDeviceSize offsets[1] = { 0 };
		// Bind vertex buffer
		vkCmdBindVertexBuffers(vulkanCmdBuffer, 0, 1, &vertexBuffer->Buffer, offsets);
	}

	TransientCommandBuffer Renderer::BeginTransientCommandBuffer(bool compute)
	{
		CommandPool* commandPool = compute ? GetComputeCommandPool() : GetGraphicsCommandPool();
		CORE_ASSERT(commandPool, "Cannot create transient command buffer without a valid CommandPool");

		VkCommandBuffer commandBuffer = commandPool->AllocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

		VkCommandBufferBeginInfo cmdBufferBeginInfo{};
		cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo));

		return TransientCommandBuffer(commandBuffer, commandPool);
	}

	void Renderer::SubmitTransientCommandBuffer(TransientCommandBuffer& commandBuffer, bool waitForQueueIdle)
	{
		commandBuffer.SubmitAndWait(waitForQueueIdle);
	}

	CommandBuffer* Renderer::GetMainCommandBuffer(bool swapchain)
	{
		return swapchain
			? s_Data->Commands.SwapchainCommandBuffer.get()
			: s_Data->Commands.MainCommandBuffer.get();
	}

	CommandPool* Renderer::GetGraphicsCommandPool()
	{
		CORE_ASSERT(s_Data && s_Data->Commands.GraphicsCommandPool, "Renderer graphics CommandPool is not initialized");
		return s_Data->Commands.GraphicsCommandPool.get();
	}

	CommandPool* Renderer::GetComputeCommandPool()
	{
		CORE_ASSERT(s_Data && s_Data->Commands.ComputeCommandPool, "Renderer compute CommandPool is not initialized");
		return s_Data->Commands.ComputeCommandPool.get();
	}

	RendererConfig& Renderer::GetRendererConfig()
	{
		static RendererConfig config;
		return config;
	}

	Ref<EnvironmentMap> Renderer::CreateEnvironmentMap(const std::string& filepath)
	{
#if 0
		// Tiering settings
		const uint32_t environmentMapResolution = 1024;
		const uint32_t irradianceMapComputeSamples = 1024;
		const uint32_t irradianceMapSize = 32;
		////////////////////////////////////////////
		// Convert equirectangular to cubemap
		////////////////////////////////////////////
		TextureProperties envEquirectProperties;
		Ref<Texture2D> envEquirect = CreateRef<Texture2D>(filepath, envEquirectProperties);
		CORE_ASSERT(envEquirect->GetFormat() == ImageFormat::RGBA32F, "Texture is not HDR!");
		TextureProperties properties;
		properties.DebugName = "Environment Map Unfiltered";
		Ref<TextureCube> envUnfiltered = CreateRef<TextureCube>(ImageFormat::RGBA32F, environmentMapResolution, environmentMapResolution, properties);
		{
			// Get compute shader and create pipeline
			Ref<Shader> equirectangularConversionShader = Renderer::GetShaderLibrary().Get("EquirectangularToCubeMap");
			Ref<ComputePipeline> equirectangularConversionPipeline = CreateRef<ComputePipeline>(equirectangularConversionShader);
			Renderer::Submit([equirectangularConversionPipeline, envEquirect, envUnfiltered, environmentMapResolution]()
			{
				VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
				Ref<Shader> shader = equirectangularConversionPipeline->GetShader();
				// Descriptors
				std::array<VkWriteDescriptorSet, 2> writeDescriptors;
				VkDescriptorSet descriptorSet = Renderer::AllocateDescriptorSet(shader->GetDescriptorSetLayout(0));
				// Set descriptor for the Output Cubemap
				writeDescriptors[0] = *shader->GetWriteDescriptorSet("o_CubeMap", 0);
				writeDescriptors[0].dstSet = descriptorSet;
				writeDescriptors[0].pImageInfo = &envUnfiltered->GetVulkanDescriptorInfo();
				// Set descriptor for the Input Equirectangular texture2D
				writeDescriptors[1] = *shader->GetWriteDescriptorSet("u_EquirectangularTex", 0);
				writeDescriptors[1].dstSet = descriptorSet;
				writeDescriptors[1].pImageInfo = &envEquirect->GetVulkanDescriptorInfo();
				// Update descriptors
				vkUpdateDescriptorSets(device, (uint32_t)writeDescriptors.size(), writeDescriptors.data(), 0, NULL);
				// Execute Compute Shader
				equirectangularConversionPipeline->Begin();
				{
					uint32_t wordGroupXY = environmentMapResolution / 32;
					equirectangularConversionPipeline->Dispatch(wordGroupXY, wordGroupXY, 6, descriptorSet);
				}
				equirectangularConversionPipeline->End();
				// Generate Mips
				envUnfiltered->GenerateMips(true);
			});
		}
		////////////////////////////////////////////
		// Filter specular environment map
		////////////////////////////////////////////
		properties.DebugName = "Environment Map Filtered";
		Ref<TextureCube> envFiltered = CreateRef<TextureCube>(ImageFormat::RGBA32F, environmentMapResolution, environmentMapResolution, properties);
		{
			// Get compute shader and create pipeline
			Ref<Shader> environmentFilteringShader = Renderer::GetShaderLibrary().Get("EnvironmentMipFilter");
			Ref<ComputePipeline> enviromentMipFilteringPipeline = CreateRef<ComputePipeline>(environmentFilteringShader);
			Renderer::Submit([enviromentMipFilteringPipeline, envFiltered, envUnfiltered, environmentMapResolution]()
			{
				VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
				Ref<Shader> shader = enviromentMipFilteringPipeline->GetShader();
				uint32_t mipCount = envFiltered->GetMipLevelCount();
				// Images infos
				VkDescriptorImageInfo filteredImageInfo = envFiltered->GetVulkanDescriptorInfo();
				VkDescriptorImageInfo unfilteredImageInfo = envUnfiltered->GetVulkanDescriptorInfo();
				
				std::vector<VkWriteDescriptorSet> writeDescriptors(mipCount * 2);
				std::vector<VkDescriptorImageInfo> mipImageInfos(mipCount);
				std::vector<VkDescriptorSet> descriptorSets(mipCount);


				for (uint32_t i = 0; i < mipCount; i++)
				{
					// Allocate descriptor
					descriptorSets[i] = Renderer::AllocateDescriptorSet(shader->GetDescriptorSetLayout(0));

					VkDescriptorImageInfo& mipImageInfo = mipImageInfos[i];
					mipImageInfo = filteredImageInfo;
					mipImageInfo.imageView = envFiltered->CreateImageViewSingleMip(i);
					// Write decriptor related to mip of the texture to filter
					writeDescriptors[i * 2 + 0] = *shader->GetWriteDescriptorSet("outputTexture", 0);
					writeDescriptors[i * 2 + 0].dstSet = descriptorSets[i];
					writeDescriptors[i * 2 + 0].pImageInfo = &mipImageInfo;
					// Write decriptor of unfiltered texture
					writeDescriptors[i * 2 + 1] = *shader->GetWriteDescriptorSet("inputTexture", 0);
					writeDescriptors[i * 2 + 1].dstSet = descriptorSets[i];
					writeDescriptors[i * 2 + 1].pImageInfo = &unfilteredImageInfo;
				}
				// Update descriptors
				vkUpdateDescriptorSets(device, (uint32_t)writeDescriptors.size(), writeDescriptors.data(), 0, NULL);

				// Compute Pass
				enviromentMipFilteringPipeline->Begin();
				{
					// Pre-filter rest of the mip-chain
					float deltaRoughness = 1.0f / glm::max((float)mipCount - 1.0f, 1.0f);
					for (uint32_t i = 0, size = environmentMapResolution; i < mipCount; i++, size /= 2)
					{
						uint32_t numGroups = glm::max(1u, (uint32_t)(size / 32));
						float roughness = i * deltaRoughness;
						roughness = glm::max(roughness, 0.05f);
						enviromentMipFilteringPipeline->SetPushConstants(&roughness, sizeof(float));
						// Sent the descriptor set of each mip invidually in order to render into it
						enviromentMipFilteringPipeline->Dispatch(numGroups, numGroups, 6, descriptorSets[i]);
					}
				}
				enviromentMipFilteringPipeline->End();
				// Image View Per Mip
				for (auto& mip : mipImageInfos)
					vkDestroyImageView(device, mip.imageView, nullptr);
			});
		}
		////////////////////////////////////////////
		// Diffuse irradiance environment map
		////////////////////////////////////////////
		properties.DebugName = "Environment Map Irradiance";
		Ref<TextureCube> envIrradiance = CreateRef<TextureCube>(ImageFormat::RGBA32F, irradianceMapSize, irradianceMapSize, properties);
		{
			// Get compute shader and create pipeline
			Ref<Shader> environmentIrradianceShader = Renderer::GetShaderLibrary().Get("EnvironmentIrradiance");
			Ref<ComputePipeline> enviromentIrradiancePipeline = CreateRef<ComputePipeline>(environmentIrradianceShader);
			Renderer::Submit([enviromentIrradiancePipeline, envIrradiance, envFiltered, irradianceMapSize, irradianceMapComputeSamples]()
			{
				VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
				Ref<Shader> shader = enviromentIrradiancePipeline->GetShader();

				// Descriptors
				std::array<VkWriteDescriptorSet, 2> writeDescriptors;
				VkDescriptorSet descriptorSet = Renderer::AllocateDescriptorSet(shader->GetDescriptorSetLayout(0));
				// Set descriptor for the Output Cubemap
				writeDescriptors[0] = *shader->GetWriteDescriptorSet("o_IrradianceMap", 0);
				writeDescriptors[0].dstSet = descriptorSet;
				writeDescriptors[0].pImageInfo = &envIrradiance->GetVulkanDescriptorInfo();
				// Set descriptor for the Input Equirectangular texture2D
				writeDescriptors[1] = *shader->GetWriteDescriptorSet("u_RadianceMap", 0);
				writeDescriptors[1].dstSet = descriptorSet;
				writeDescriptors[1].pImageInfo = &envFiltered->GetVulkanDescriptorInfo();
				// Update descriptors
				vkUpdateDescriptorSets(device, (uint32_t)writeDescriptors.size(), writeDescriptors.data(), 0, NULL);
				
				// Compute Pass
				enviromentIrradiancePipeline->Begin();
				{
					enviromentIrradiancePipeline->SetPushConstants(&irradianceMapComputeSamples, sizeof(uint32_t));
					enviromentIrradiancePipeline->Dispatch(irradianceMapSize / 32, irradianceMapSize / 32, 6, descriptorSet);
				}
				enviromentIrradiancePipeline->End();
				envIrradiance->GenerateMips(true);
			});
		}

		Ref<EnvironmentMap> environment = CreateRef<EnvironmentMap>();
		environment->EquirectangularTex = envEquirect;
		environment->RadianceMap = envFiltered;
		environment->IrradianceMap = envIrradiance;
		environment->DebugName = utils::GetFilename(filepath);
		return environment;
#endif

		return nullptr;
	}

	void Renderer::SetSceneEnvironment(SceneRenderer* sceneRenderer, const Ref<EnvironmentMap>& environment, const Ref<Image>& shadowImage)
	{
#if OLD_RENDERER
		Renderer::Submit([sceneRenderer, environment, shadowImage]()
		{
			Ref<Shader> pbrShader = Renderer::GetShaderLibrary().Get("PBR");
			const uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			// Change to a persistent pool
			if (s_Data->Descriptors.RendererDescriptorSetCache.find(sceneRenderer) == s_Data->Descriptors.RendererDescriptorSetCache.end())
			{
				const uint32_t framesInFlight = Renderer::GetRendererConfig().FramesInFlight;
				s_Data->Descriptors.RendererDescriptorSetCache[sceneRenderer].resize(framesInFlight);
				for (uint32_t i = 0; i < framesInFlight; i++)
				{
					VkDescriptorSet descriptorSet = Renderer::AllocateDescriptorSet(pbrShader->GetDescriptorSetLayout(1), true);
					s_Data->Descriptors.RendererDescriptorSetCache.at(sceneRenderer)[i].push_back(descriptorSet);
				}
					
			}
			VkDescriptorSet descriptorSet = s_Data->Descriptors.RendererDescriptorSetCache.at(sceneRenderer)[currentFrame][0];
			// Dynamic pool allocation
			// VkDescriptorSet descriptorSet = Renderer::AllocateDescriptorSet(pbrShader->GetDescriptorSetLayout(1));
			s_Data->Descriptors.ActiveRendererDescriptorSet = descriptorSet;

			std::array<VkWriteDescriptorSet, 4> writeDescriptors;

			Ref<TextureCube> radianceMap = environment->RadianceMap;
			Ref<TextureCube> irradianceMap = environment->IrradianceMap;
			// Environment Radiance Texture
			writeDescriptors[0] = *pbrShader->GetWriteDescriptorSet("u_EnvRadianceTex", 1);
			writeDescriptors[0].dstSet = descriptorSet;
			const auto& radianceMapImageInfo = radianceMap->GetVulkanDescriptorInfo();
			writeDescriptors[0].pImageInfo = &radianceMapImageInfo;
			// Environment Irradiance Texture
			writeDescriptors[1] = *pbrShader->GetWriteDescriptorSet("u_EnvIrradianceTex", 1);
			writeDescriptors[1].dstSet = descriptorSet;
			const auto& irradianceMapImageInfo = irradianceMap->GetVulkanDescriptorInfo();
			writeDescriptors[1].pImageInfo = &irradianceMapImageInfo;
			// BRDF LUT Texture
			writeDescriptors[2] = *pbrShader->GetWriteDescriptorSet("u_BRDFLUTTex", 1);
			writeDescriptors[2].dstSet = descriptorSet;
			const auto& BrdfLutImageInfo= RendererResources::GetBRDFLutTexture()->GetVulkanDescriptorInfo();
			writeDescriptors[2].pImageInfo = &BrdfLutImageInfo;
			// Shadow Map
			// Environment Radiance Texture
			writeDescriptors[3] = *pbrShader->GetWriteDescriptorSet("u_ShadowMapTex", 1);
			writeDescriptors[3].dstSet = descriptorSet;
			const auto& shadowImageInfo = shadowImage->GetVulkanDescriptorInfo();
			writeDescriptors[3].pImageInfo = &shadowImageInfo;
			// Update descriptor
			VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			vkUpdateDescriptorSets(vulkanDevice, (uint32_t)writeDescriptors.size(), writeDescriptors.data(), 0, nullptr);
		});
#endif
	}

	uint32_t Renderer::GetCurrentFrameIndex()
	{
		return Window::GetCurrentSwapchain()->GetCurrentBufferIndex();
	}



	void Renderer::RenderFullscreenQuad(CommandBuffer* commandBuffer, Handle<GraphicsPipeline> hdPipeline, Handle<BindGroup> hdBindGroup)
	{
		uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
		VkCommandBuffer vulkanCommandBuffer = commandBuffer->GetCommandBuffer(currentFrame);

		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(hdPipeline);

		// Bind Pipeline to be used in render pass
		vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());

		
		// Descriptor Sets
		BindGroup* bindGroup = ResourceManager::GetBindGroup(hdBindGroup);
		if (bindGroup)
		{
			bindGroup->Update(currentFrame);
			VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
			VkDescriptorSet descriptorSets = bindGroup->GetHandle(currentFrame);
			vkCmdBindDescriptorSets(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets, 0, nullptr);
		}
		// Bind Material push constant (Fragment)
		/** /
		Buffer materialPushConstantBuffer = material->GetUniformBufferStorage();
		if (materialPushConstantBuffer.Size)
			vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, materialPushConstantBuffer.Size, materialPushConstantBuffer.Data);
		/**/
		vkCmdDraw(vulkanCommandBuffer, 3, 1, 0, 0);

#if OLD_RENDERER
		Renderer::Submit([CommandBuffer, uniformBufferSet, pipeline, material]() mutable
		{
			uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			VkCommandBuffer vulkanCommandBuffer = CommandBuffer->GetCommandBuffer(currentFrame);
			// Bind Pipeline to be used in render pass
			vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetVulkanPipeline());

			// Update Material
			Renderer::UpdateMaterialForRendering(material, uniformBufferSet);
			// Descriptor Sets
			VkDescriptorSet descriptorSets = material->GetDescriptorSet(currentFrame);
			VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
			// Bind the descriptor
			if(descriptorSets)
				vkCmdBindDescriptorSets(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets, 0, nullptr);

			// Bind Material push constant (Fragment)
			Buffer materialPushConstantBuffer = material->GetUniformBufferStorage();
			if (materialPushConstantBuffer.Size)
				vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, materialPushConstantBuffer.Size, materialPushConstantBuffer.Data);
			
			vkCmdDraw(vulkanCommandBuffer, 3, 1, 0, 0);
		});
#endif
	}

	void Renderer::RenderRenderPass(CommandBuffer* commandBuffer, const DrawArea& drawArea, std::vector<uint32_t> drawStream)
	{
		uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
		VkCommandBuffer vulkanCommandBuffer = commandBuffer->GetCommandBuffer(currentFrame);

		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(drawArea.GraphicsPipeline);
		CORE_ASSERT(pipeline, "Null graphics pipeline");
		// Bind Pipeline to be used in render pass
		vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());

		uint32_t* stream = drawStream.data();
		DrawCallInfo drawCallInfo;
		for (uint32_t index = 0; index < drawArea.DrawCount; index++)
		{
			uint16_t dirtyFlags = (uint16_t)*stream;
			uint32_t dirtyCount = utils::GetCountOfBits(dirtyFlags);

			stream += sizeof(uint32_t);
			if (dirtyFlags & DrawStreamFlags::VERTEX_BUFFER)
			{
				drawCallInfo.VertexBuffer = *(Handle<GPUBuffer>*)&stream;
				stream += sizeof(uint32_t);

				GPUBuffer* vertexBuffer = ResourceManager::GetBuffer(drawCallInfo.VertexBuffer);
				CORE_ASSERT(vertexBuffer, "Null GPU vertex buffer");
				VkDeviceSize offsets[1] = { 0 };
				// Bind vertex buffer
				vkCmdBindVertexBuffers(vulkanCommandBuffer, 0, 1, &vertexBuffer->Buffer, offsets);
			}
			if (dirtyFlags & DrawStreamFlags::INDEX_BUFFER)
			{
				drawCallInfo.IndexBuffer = *(Handle<GPUBuffer>*)&stream;
				stream += sizeof(uint32_t);

				GPUBuffer* indexBuffer = ResourceManager::GetBuffer(drawCallInfo.IndexBuffer);
				CORE_ASSERT(indexBuffer, "Null GPU index buffer");
				VkDeviceSize offsets[1] = { 0 };
				// VK_INDEX_TYPE_UINT32 : The type of data that index buffer stores
				// Bind index buffer
				vkCmdBindIndexBuffer(vulkanCommandBuffer, indexBuffer->Buffer, 0, VK_INDEX_TYPE_UINT32);
			}


			if (dirtyFlags & DrawStreamFlags::VERTEX_OFFSET)
			{
				drawCallInfo.VertexOffset = *stream;
				stream += sizeof(uint32_t);
			}
			if (dirtyFlags & DrawStreamFlags::INDEX_OFFSET)
			{
				drawCallInfo.IndexOffset = *stream;
				stream += sizeof(uint32_t);
			}
			
			if (dirtyFlags & DrawStreamFlags::INDEX_COUNT) 
			{
				drawCallInfo.IndexCount = *stream;
				stream += sizeof(uint32_t);
			}

			// Draw call
			vkCmdDrawIndexed(vulkanCommandBuffer, drawCallInfo.IndexCount, 1, drawCallInfo.IndexOffset, drawCallInfo.VertexOffset, 0);
			s_Data->Frame.DrawCallCount++;

		}

	}

	void Renderer::RenderMeshInstances(CommandBuffer* commandBuffer, const DrawArea& drawArea, std::vector<Handle<MeshInstanceProxy>> drawStream)
	{
		uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
		VkCommandBuffer vulkanCommandBuffer = commandBuffer->GetCommandBuffer(currentFrame);
#if 0
		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(drawArea.GraphicsPipeline);
		CORE_ASSERT(pipeline, "Null graphics pipeline");
		// Bind Pipeline to be used in render pass
		vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());


		CORE_ASSERT(drawStream.size(), "Empty draw stream");
		MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(drawStream[0]);
		MeshSourceProxy* meshSource = ResourceManager::GetMeshSourceProxy(meshInstance->MeshSource);

		VkDeviceSize offsets[1] = { 0 };
		VkBuffer vertexBuffer = meshSource->VertexBuffer->GetHandle();
		// Bind vertex buffer
		vkCmdBindVertexBuffers(vulkanCommandBuffer, 0, 1, &vertexBuffer, offsets);


		VkBuffer indexBuffer = meshSource->IndexBuffer->GetHandle();
		// Bind vertex buffer
		vkCmdBindIndexBuffer(vulkanCommandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		// Bind Descriptor Sets
		VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
		BindGroup* bindGroup0 = ResourceManager::GetBindGroup(drawArea.BindGroup0);
		BindGroup* bindGroup1 = ResourceManager::GetBindGroup(drawArea.BindGroup1);
		std::array<VkDescriptorSet, 2> descriptorSets =
		{
			bindGroup0->GetHandle(currentFrame),
			bindGroup1->GetHandle(currentFrame)

		};

		// Bind the descriptor set
		vkCmdBindDescriptorSets(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, (uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
		
		CORE_ASSERT(drawStream.size(), "Empty draw stream");
#endif	
		MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(drawStream[0]);
		MeshSourceProxy* meshSource = ResourceManager::GetMeshSourceProxy(meshInstance->MeshSource);
		SubmeshSourceProxy submeshSource = meshSource->Submeshes[meshInstance->SubmeshIndex];

		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(drawArea.GraphicsPipeline);
		VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();

		for (uint32_t index = 0; index < drawStream.size(); index++)
		{
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(drawStream[index]);
			// Bind Transform push constant (Vertex)
			glm::mat4 worldTransform = meshInstance->WorldTransform;
			vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &worldTransform);

			// Draw call
			vkCmdDrawIndexed(vulkanCommandBuffer, submeshSource.IndexCount, 1, submeshSource.BaseIndex, submeshSource.BaseVertex, 0);
			s_Data->Frame.DrawCallCount++;

		}

	}

	/** /
	void Renderer::RenderMesh(Ref<CommandBuffer> CommandBuffer, Ref<UniformBufferSet> uniformBufferSet, Ref<GraphicsPipeline> pipeline, Ref<Mesh> mesh, Ref<MaterialTable> materialTable, glm::mat4& transform)
	{
		Renderer::Submit([CommandBuffer, uniformBufferSet, pipeline, mesh, materialTable, transform]() mutable
		{
#if OLD_RENDERER
			Ref<MeshSource> meshSource = mesh->GetMeshSource();
			uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			VkCommandBuffer vulkanCommandBuffer = CommandBuffer->GetCommandBuffer(currentFrame);
			// Bind Pipeline to be used in render pass
			vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetVulkanPipeline());

			VkBuffer vbMeshBuffer = meshSource->GetVertexBuffer()->GetVulkanBuffer();
			VkDeviceSize offsets[1] = { 0 };
			// Bind vertex buffer
			vkCmdBindVertexBuffers(vulkanCommandBuffer, 0, 1, &vbMeshBuffer, offsets);

			VkBuffer ibMeshBuffer = meshSource->GetIndexBuffer()->GetVulkanBuffer();
			// VK_INDEX_TYPE_UINT32 : The type of data that index buffer stores
			// Bind index buffer
			vkCmdBindIndexBuffer(vulkanCommandBuffer, ibMeshBuffer, 0, VK_INDEX_TYPE_UINT32);

			auto& subMeshes = meshSource->GetSubmeshes();
			Ref<MaterialTable> materialTableMesh = materialTable;
			// Update Descripor set per material
			for (uint32_t i = 0; i < materialTableMesh->GetMaterialCount(); i++)
			{
				Ref<MaterialAsset> materialAsset = materialTableMesh->GetMaterial(i);
				Renderer::UpdateMaterialForRendering(materialAsset->GetMaterial(), uniformBufferSet);
			}

			for (uint32_t index = 0; index < subMeshes.size(); index++)
			{
				// Update Material
				const Submesh& submesh = subMeshes[index];
				Ref<Material> material = materialTableMesh->GetMaterial(submesh.MaterialIndex)->GetMaterial();
				// Bind Descriptor Sets
				VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
				std::array<VkDescriptorSet, 2> descriptorSets = 
				{	
					material->GetDescriptorSet(currentFrame),		// Set 0
					s_Data->Descriptors.ActiveRendererDescriptorSet						// Set 1 (Environment)
				};

				// Bind the descriptor set
				vkCmdBindDescriptorSets(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
					0, (uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);

				// Bind Transform push constant (Vertex)
				glm::mat4 worldTransform = transform * submesh.Transform;
				vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &worldTransform);
				// Bind Material push constant (Fragment)
				Buffer materialPushConstantBuffer = material->GetUniformBufferStorage();
				vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4), materialPushConstantBuffer.Size, materialPushConstantBuffer.Data);
					
				// Draw call
				vkCmdDrawIndexed(vulkanCommandBuffer, submesh.IndexCount, 1, submesh.BaseIndex, submesh.BaseVertex, 0);
				s_Data->Frame.DrawCallCount++;
			}
#endif
		});
	}

	void Renderer::RenderMeshWithMaterial(Ref<CommandBuffer> CommandBuffer, Ref<UniformBufferSet> uniformBufferSet, Ref<Pipeline> pipeline, Ref<Mesh> mesh, glm::mat4& transform, Ref<Material> material, const Buffer& additionalUniforms)
	{
#if OLD_RENDERER
		Buffer pushConstantBuffer;
		pushConstantBuffer.Allocate(sizeof(glm::mat4) + additionalUniforms.Size);
		if (additionalUniforms.Size)
			pushConstantBuffer.Write(additionalUniforms.Data, additionalUniforms.Size, sizeof(glm::mat4));

		Renderer::Submit([CommandBuffer, uniformBufferSet, pipeline, mesh, transform, material, pushConstantBuffer]() mutable
		{
			Ref<MeshSource> meshSource = mesh->GetMeshSource();
			uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			VkCommandBuffer vulkanCommandBuffer = CommandBuffer->GetCommandBuffer(currentFrame);
			// Bind Pipeline to be used in render pass
			vkCmdBindPipeline(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetVulkanPipeline());

			VkBuffer vbMeshBuffer = meshSource->GetVertexBuffer()->GetVulkanBuffer();
			VkDeviceSize offsets[1] = { 0 };
			// Bind vertex buffer
			vkCmdBindVertexBuffers(vulkanCommandBuffer, 0, 1, &vbMeshBuffer, offsets);

			VkBuffer ibMeshBuffer = meshSource->GetIndexBuffer()->GetVulkanBuffer();
			// VK_INDEX_TYPE_UINT32 : The type of data that index buffer stores
			// Bind index buffer
			vkCmdBindIndexBuffer(vulkanCommandBuffer, ibMeshBuffer, 0, VK_INDEX_TYPE_UINT32);

			
			auto& subMeshes = meshSource->GetSubmeshes();
			// Update Descripor set per material
			Renderer::UpdateMaterialForRendering(material, uniformBufferSet);

			// Bind Descriptor Sets
			VkPipelineLayout pipelineLayout = pipeline->GetVulkanPipelineLayout();
			VkDescriptorSet descriptorSet = material->GetDescriptorSet(currentFrame);

			// Bind the descriptor set
			if (descriptorSet)
				vkCmdBindDescriptorSets(vulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
					0, 1, &descriptorSet, 0, nullptr);
			// Bind Material push constant (Fragment)
			Buffer materialPushConstantBuffer = material->GetUniformBufferStorage();
			if (materialPushConstantBuffer)
				vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, pushConstantBuffer.Size, materialPushConstantBuffer.Size, materialPushConstantBuffer.Data);

			for (uint32_t index = 0; index < subMeshes.size(); index++)
			{
				// Update Material
				const Submesh& submesh = subMeshes[index];
				// Bind Transform push constant (Vertex)
				glm::mat4 worldTransform = transform * submesh.Transform;
				pushConstantBuffer.Write(&worldTransform, sizeof(glm::mat4), 0);
				vkCmdPushConstants(vulkanCommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, pushConstantBuffer.Size, pushConstantBuffer.Data);
				// Draw call
				vkCmdDrawIndexed(vulkanCommandBuffer, submesh.IndexCount, 1, submesh.BaseIndex, submesh.BaseVertex, 0);
				s_Data->Frame.DrawCallCount++;
			}
			pushConstantBuffer.Release();
		});

#endif
	}
	/**/
	void Renderer::DispatchComputeShader(Ref<CommandBuffer> CommandBuffer, Ref<ComputePipeline> pipeline, Ref<UniformBufferSet> uniformBufferSet, Ref<Material> material, const glm::ivec3& workGroups, Buffer pushConstantBuffer)
	{
#if OLD_RENDERER
		Renderer::Submit([CommandBuffer, uniformBufferSet, pipeline, workGroups, material, pushConstantBuffer]() mutable
		{
			uint32_t currentFrame = Renderer::GetCurrentFrameIndex();
			// Update Descripor set per material
			Renderer::UpdateMaterialForRendering(material, uniformBufferSet);
			VkDescriptorSet descriptorSet = material->GetDescriptorSet(currentFrame);
			pipeline->Begin(CommandBuffer);
			{
				if (pushConstantBuffer.Size)
					pipeline->SetPushConstants(pushConstantBuffer.Data, pushConstantBuffer.Size);

				pipeline->Dispatch(workGroups.x, workGroups.y, workGroups.z, descriptorSet);
			}
			pipeline->End();

		});
#endif
	}

	VkPhysicalDeviceLimits Renderer::GetDeviceLimits()
	{
		return s_Data->Frame.DeviceLimits;
	}
}