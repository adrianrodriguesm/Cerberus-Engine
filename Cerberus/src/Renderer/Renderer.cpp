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
#include "Renderer/TextureAsset.h"
#include "Renderer/ShaderCompiler.h"

namespace cerberus
{
	struct RendererData
	{
		RendererCommandState Commands;
		RendererDescriptorState Descriptors;
		RendererFrameState Frame;
	};

	static RendererData* s_Data;

	namespace utils
	{
		uint32_t FindImageBinding(
			const std::vector<BindGroupLayout::ImageBinding>& bindings,
			const std::string& name)
		{
			for (const BindGroupLayout::ImageBinding& binding : bindings)
			{
				if (binding.Name == name)
					return binding.BindingPoint;
			}

			CORE_ASSERT(false, "Environment compute shader binding was not reflected");
			return 0;
		}

		Handle<ImageView> CreateEnvironmentCubeMipView(Handle<TextureCube> textureHandle, uint32_t mip)
		{
			TextureCube* texture = ResourceManager::GetTextureCube(textureHandle);
			CORE_ASSERT(texture, "Invalid environment TextureCube");
			CORE_ASSERT(mip < texture->GetMipLevelCount(), "Invalid environment cube mip");

			ImageViewProperties viewProperties{};
			viewProperties.Image = texture->GetImage();
			viewProperties.BaseMip = mip;
			viewProperties.MipCount = 1;
			viewProperties.BaseLayer = 0;
			viewProperties.LayerCount = 6;
			viewProperties.ViewType = ImageViewType::ImageCube;
			viewProperties.SamplerSpec.ShaderResource = false;

			Handle<ImageView> imageView = ResourceManager::CreateImageView(viewProperties);
			ImageView* resource = ResourceManager::GetImageView(imageView);
			CORE_ASSERT(resource, "Failed to create environment cube mip view");
			resource->Invalidate();
			resource->UpdateDescriptor();
			return imageView;
		}

		void InsertEnvironmentComputeReadBarrier(Handle<TextureCube> textureHandle)
		{
			TextureCube* texture = ResourceManager::GetTextureCube(textureHandle);
			CORE_ASSERT(texture, "Invalid environment TextureCube");

			Image* image = ResourceManager::GetImage(texture->GetImage());
			CORE_ASSERT(image, "Environment TextureCube has an invalid image");

			VkImageSubresourceRange range{};
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseMipLevel = 0;
			range.levelCount = texture->GetMipLevelCount();
			range.baseArrayLayer = 0;
			range.layerCount = 6;

			TransientCommandBuffer commandBuffer = Renderer::BeginTransientCommandBuffer();
			utils::InsertImageMemoryBarrier(
				commandBuffer,
				image->GetHandle(),
				VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				range);
			commandBuffer.SubmitAndWait();
		}

		Ref<EnvironmentMap> CreateEnvironmentMapResource(const std::string& filepath)
		{
			const RendererConfig& config = Renderer::GetRendererConfig();

			TextureMetadata sourceMetadata;
			sourceMetadata.DebugName = utils::GetFilename(filepath);
			sourceMetadata.FilePath = filepath;
			sourceMetadata.TextureType = TextureType::Texture2D;
			sourceMetadata.SamplerWrap = SamplerWrap::Clamp;
			sourceMetadata.SamplerFilter = SamplerFilter::Linear;
			sourceMetadata.GenerateMips = false;
			sourceMetadata.SRGB = false;

			Ref<TextureAsset> sourceAsset = CreateRef<TextureAsset>(sourceMetadata);
			if (!sourceAsset->Loaded())
			{
				NV_CORE_ERROR("Failed to load environment map source: {0}", filepath);
				return nullptr;
			}

			if (sourceAsset->GetFormat() != ImageFormat::RGBA32F)
			{
				NV_CORE_ERROR("Environment map must be an HDR texture: {0}", filepath);
				return nullptr;
			}

			Handle<Texture2D> equirectangularTexture =
				ResourceManager::CreateTexture2D(sourceAsset, TextureColorSpace::Linear);

			TextureProperties cubeProperties;
			cubeProperties.DebugName = "Environment Map Unfiltered";
			cubeProperties.SamplerWrap = SamplerWrap::Clamp;
			cubeProperties.SamplerFilter = SamplerFilter::Linear;
			cubeProperties.GenerateMips = true;
			cubeProperties.Storage = true;

			Handle<TextureCube> unfilteredMap = ResourceManager::CreateTextureCube(
				ImageFormat::RGBA32F,
				config.EnvironmentMapResolution,
				config.EnvironmentMapResolution,
				cubeProperties);

			// Equirectangular HDR -> unfiltered cubemap mip 0.
			{
				Ref<Shader> shader = RendererResources::GetShaderLibrary().Get("EquirectangularToCubeMap");
				BindGroupLayout::BindGroupLayoutInfo layoutInfo = ShaderReflection::ReflectLayoutInfo(shader, 0);
				const uint32_t outputBinding = FindImageBinding(layoutInfo.ImageStorages, "o_CubeMap");
				const uint32_t inputBinding = FindImageBinding(layoutInfo.ImageSamplers, "u_EquirectangularTex");

				Handle<BindGroupLayout> layout = ResourceManager::CreateBindGroupLayout(layoutInfo);
				BindGroupSpecification bindGroupSpecification;
				bindGroupSpecification.BindGroupLayout = layout;
				bindGroupSpecification.Lifetime = DescriptorAllocationLifetime::Transient;
				Handle<BindGroup> bindGroupHandle = ResourceManager::CreateBindGroup(bindGroupSpecification);

				Handle<ImageView> outputMipView = CreateEnvironmentCubeMipView(unfilteredMap, 0);
				Texture2D* inputTexture = ResourceManager::GetTexture2D(equirectangularTexture);
				BindGroup* bindGroup = ResourceManager::GetBindGroup(bindGroupHandle);
				CORE_ASSERT(inputTexture && bindGroup, "Failed to prepare environment conversion resources");

				bindGroup->SetImageView(outputMipView, outputBinding);
				bindGroup->SetImageView(inputTexture->GetImageView(), inputBinding);
				bindGroup->Update(0);

				Ref<ComputePipeline> pipeline = CreateRef<ComputePipeline>(shader);
				RenderQueueManager::ExecuteRenderQueue();
				pipeline->Begin();
				const uint32_t groups = (config.EnvironmentMapResolution + 31u) / 32u;
				pipeline->Dispatch(groups, groups, 6, bindGroup->GetHandle(0));
				pipeline->End();

				ResourceManager::DestroyBindGroup(bindGroupHandle);
				ResourceManager::DestroyBindGroupLayout(layout);
				ResourceManager::DestroyImageView(outputMipView);
			}

			TextureCube* unfilteredTexture = ResourceManager::GetTextureCube(unfilteredMap);
			CORE_ASSERT(unfilteredTexture, "Failed to create unfiltered environment map");
			unfilteredTexture->GenerateMips(false);

			cubeProperties.DebugName = "Environment Map Filtered";
			Handle<TextureCube> filteredMap = ResourceManager::CreateTextureCube(
				ImageFormat::RGBA32F,
				config.EnvironmentMapResolution,
				config.EnvironmentMapResolution,
				cubeProperties);

			// GGX-filter every radiance mip.
			{
				Ref<Shader> shader = RendererResources::GetShaderLibrary().Get("EnvironmentMipFilter");
				BindGroupLayout::BindGroupLayoutInfo layoutInfo = ShaderReflection::ReflectLayoutInfo(shader, 0);
				const uint32_t outputBinding = FindImageBinding(layoutInfo.ImageStorages, "outputTexture");
				const uint32_t inputBinding = FindImageBinding(layoutInfo.ImageSamplers, "inputTexture");

				Handle<BindGroupLayout> layout = ResourceManager::CreateBindGroupLayout(layoutInfo);
				Ref<ComputePipeline> pipeline = CreateRef<ComputePipeline>(shader);
				RenderQueueManager::ExecuteRenderQueue();

				TextureCube* filteredTexture = ResourceManager::GetTextureCube(filteredMap);
				CORE_ASSERT(filteredTexture, "Failed to create filtered environment map");
				const uint32_t mipCount = filteredTexture->GetMipLevelCount();
				const float deltaRoughness = 1.0f / glm::max(static_cast<float>(mipCount) - 1.0f, 1.0f);

				std::vector<Handle<BindGroup>> bindGroups;
				std::vector<Handle<ImageView>> mipViews;
				bindGroups.reserve(mipCount);
				mipViews.reserve(mipCount);

				for (uint32_t mip = 0; mip < mipCount; mip++)
				{
					BindGroupSpecification bindGroupSpecification;
					bindGroupSpecification.BindGroupLayout = layout;
					bindGroupSpecification.Lifetime = DescriptorAllocationLifetime::Transient;
					Handle<BindGroup> bindGroupHandle = ResourceManager::CreateBindGroup(bindGroupSpecification);
					Handle<ImageView> mipView = CreateEnvironmentCubeMipView(filteredMap, mip);

					BindGroup* bindGroup = ResourceManager::GetBindGroup(bindGroupHandle);
					CORE_ASSERT(bindGroup, "Failed to create environment filter bind group");
					bindGroup->SetImageView(mipView, outputBinding);
					bindGroup->SetImageView(unfilteredTexture->GetImageView(), inputBinding);
					bindGroup->Update(0);

					bindGroups.push_back(bindGroupHandle);
					mipViews.push_back(mipView);
				}

				pipeline->Begin();
				for (uint32_t mip = 0; mip < mipCount; mip++)
				{
					const uint32_t mipSize = glm::max(1u, config.EnvironmentMapResolution >> mip);
					const uint32_t groups = glm::max(1u, (mipSize + 31u) / 32u);
					const float roughness = glm::max(static_cast<float>(mip) * deltaRoughness, 0.05f);
					pipeline->SetPushConstants(&roughness, sizeof(float));

					BindGroup* bindGroup = ResourceManager::GetBindGroup(bindGroups[mip]);
					pipeline->Dispatch(groups, groups, 6, bindGroup->GetHandle(0));
				}
				pipeline->End();

				for (Handle<BindGroup> bindGroup : bindGroups)
					ResourceManager::DestroyBindGroup(bindGroup);
				for (Handle<ImageView> mipView : mipViews)
					ResourceManager::DestroyImageView(mipView);
				ResourceManager::DestroyBindGroupLayout(layout);
			}

			InsertEnvironmentComputeReadBarrier(filteredMap);

			TextureProperties irradianceProperties = cubeProperties;
			irradianceProperties.DebugName = "Environment Map Irradiance";
			irradianceProperties.GenerateMips = false;
			Handle<TextureCube> irradianceMap = ResourceManager::CreateTextureCube(
				ImageFormat::RGBA32F,
				config.IrradianceMapSize,
				config.IrradianceMapSize,
				irradianceProperties);

			// Diffuse irradiance convolution.
			{
				Ref<Shader> shader = RendererResources::GetShaderLibrary().Get("EnvironmentIrradiance");
				BindGroupLayout::BindGroupLayoutInfo layoutInfo = ShaderReflection::ReflectLayoutInfo(shader, 0);
				const uint32_t outputBinding = FindImageBinding(layoutInfo.ImageStorages, "o_IrradianceMap");
				const uint32_t inputBinding = FindImageBinding(layoutInfo.ImageSamplers, "u_RadianceMap");

				Handle<BindGroupLayout> layout = ResourceManager::CreateBindGroupLayout(layoutInfo);
				BindGroupSpecification bindGroupSpecification;
				bindGroupSpecification.BindGroupLayout = layout;
				bindGroupSpecification.Lifetime = DescriptorAllocationLifetime::Transient;
				Handle<BindGroup> bindGroupHandle = ResourceManager::CreateBindGroup(bindGroupSpecification);

				TextureCube* filteredTexture = ResourceManager::GetTextureCube(filteredMap);
				TextureCube* irradianceTexture = ResourceManager::GetTextureCube(irradianceMap);
				BindGroup* bindGroup = ResourceManager::GetBindGroup(bindGroupHandle);
				CORE_ASSERT(filteredTexture && irradianceTexture && bindGroup, "Failed to prepare irradiance resources");

				bindGroup->SetImageView(irradianceTexture->GetImageView(), outputBinding);
				bindGroup->SetImageView(filteredTexture->GetImageView(), inputBinding);
				bindGroup->Update(0);

				Ref<ComputePipeline> pipeline = CreateRef<ComputePipeline>(shader);
				RenderQueueManager::ExecuteRenderQueue();
				pipeline->Begin();
				pipeline->SetPushConstants(&config.IrradianceMapComputeSamples, sizeof(uint32_t));
				const uint32_t groups = (config.IrradianceMapSize + 31u) / 32u;
				pipeline->Dispatch(groups, groups, 6, bindGroup->GetHandle(0));
				pipeline->End();

				ResourceManager::DestroyBindGroup(bindGroupHandle);
				ResourceManager::DestroyBindGroupLayout(layout);
			}

			InsertEnvironmentComputeReadBarrier(irradianceMap);
			ResourceManager::DestroyTextureCube(unfilteredMap);

			Ref<EnvironmentMap> environment = CreateRef<EnvironmentMap>(
				equirectangularTexture,
				filteredMap,
				irradianceMap);
			environment->DebugName = utils::GetFilename(filepath);
			return environment;
		}
	}

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
		return utils::CreateEnvironmentMapResource(filepath);
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
