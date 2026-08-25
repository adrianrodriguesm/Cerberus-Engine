#include "pch.h"
#include "GraphicsCommandList.h"

#include "Renderer/Core/RendererContext.h"
#include "Renderer/RHI/ResourceManager.h"

#include <cmath>

namespace cerberus
{
	namespace
	{
		// Shared vertex-stage push-constant shape for every DrawStream consumer.
		// Depth-only pipelines (PreDepth, Shadow) declare a push-constant range
		// covering only the leading mat4; Forward's covers the whole struct. Normal
		// matrices are derived in ForwardPBR.vert from WorldTransform instead of
		// being precomputed CPU-side, so this stays the one shape every pass shares.
		struct DrawStreamPushConstants
		{
			glm::mat4 WorldTransform = glm::mat4(1.0f);
			uint32_t MaterialIndex = 0;
		};

		static_assert(sizeof(DrawStreamPushConstants) == 68,
			"DrawStreamPushConstants must match the Forward/PreDepth/Shadow vertex shaders");
	}

	GraphicsCommandList::GraphicsCommandList(
		CommandBufferView commandBuffer,
		RendererFrameStatistics* statistics)
		: CommandListBase(commandBuffer, statistics)
	{
	}

	namespace
	{
		VkAttachmentLoadOp VulkanLoadOp(LOAD_OP loadOp)
		{
			switch (loadOp)
			{
			case LOAD_OP::CLEAR:		return VK_ATTACHMENT_LOAD_OP_CLEAR;
			case LOAD_OP::LOAD:			return VK_ATTACHMENT_LOAD_OP_LOAD;
			case LOAD_OP::DONT_CARE:	return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			case LOAD_OP::NONE:			return VK_ATTACHMENT_LOAD_OP_NONE_EXT;
			}
			CORE_ASSERT(false, "Load operation does not exist!");
			return VK_ATTACHMENT_LOAD_OP_NONE_EXT;
		}

		VkAttachmentStoreOp VulkanStoreOp(STORE_OP storeOp)
		{
			switch (storeOp)
			{
			case STORE_OP::STORE:		return VK_ATTACHMENT_STORE_OP_STORE;
			case STORE_OP::DONT_CARE:	return VK_ATTACHMENT_STORE_OP_DONT_CARE;
			case STORE_OP::NONE:		return VK_ATTACHMENT_STORE_OP_NONE;
			}
			CORE_ASSERT(false, "Store operation does not exist!");
			return VK_ATTACHMENT_STORE_OP_NONE;
		}

		VkImageSubresourceRange ColorSubresourceRange()
		{
			VkImageSubresourceRange range{};
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseMipLevel = 0;
			range.levelCount = 1;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			return range;
		}

		VkImageSubresourceRange DepthSubresourceRange()
		{
			VkImageSubresourceRange range{};
			range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			range.baseMipLevel = 0;
			range.levelCount = 1;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			return range;
		}

		// Shared by BeginRendering (which barriers each attachment itself) and
		// BeginRenderingAttachments (which assumes the caller already did).
		VkRenderingAttachmentInfo BuildColorAttachmentInfo(
			const RenderingAttachmentInfo& colorAttachmentInfo)
		{
			RenderTargetTexture* target = ResourceManager::GetRenderTarget(colorAttachmentInfo.Target);
			CORE_ASSERT(target, "Rendering scope received an invalid color attachment");
			ImageView* imageView = ResourceManager::GetImageView(target->GetImageView());
			CORE_ASSERT(imageView, "Color attachment render target has no image view");

			VkRenderingAttachmentInfo attachment{};
			attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			attachment.imageView = imageView->GetHandle();
			attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment.resolveMode = VK_RESOLVE_MODE_NONE;
			attachment.loadOp = VulkanLoadOp(colorAttachmentInfo.LoadOp);
			attachment.storeOp = VulkanStoreOp(colorAttachmentInfo.StoreOp);
			const glm::vec4 clearColor = target->GetClearColor();
			attachment.clearValue.color = { { clearColor.r, clearColor.g, clearColor.b, clearColor.a } };
			return attachment;
		}

		VkRenderingAttachmentInfo BuildDepthAttachmentInfo(
			const RenderingAttachmentInfo& depthAttachmentInfo)
		{
			RenderTargetTexture* target = ResourceManager::GetRenderTarget(depthAttachmentInfo.Target);
			CORE_ASSERT(target, "Rendering scope received an invalid depth attachment");
			ImageView* imageView = ResourceManager::GetImageView(target->GetImageView());
			CORE_ASSERT(imageView, "Depth attachment render target has no image view");

			VkRenderingAttachmentInfo attachment{};
			attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			attachment.imageView = imageView->GetHandle();
			attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			attachment.resolveMode = VK_RESOLVE_MODE_NONE;
			attachment.loadOp = VulkanLoadOp(depthAttachmentInfo.LoadOp);
			attachment.storeOp = VulkanStoreOp(depthAttachmentInfo.StoreOp);
			// Matches the depth clear the Framebuffer wrapper used unconditionally.
			attachment.clearValue.depthStencil = { 1.0f, 0 };
			return attachment;
		}

		void SetViewportAndScissor(GraphicsCommandList& commandList, uint32_t width, uint32_t height)
		{
			VkViewport viewport{};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = static_cast<float>(width);
			viewport.height = static_cast<float>(height);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			commandList.SetViewport(viewport);

			VkRect2D scissor{};
			scissor.offset = { 0, 0 };
			scissor.extent = { width, height };
			commandList.SetScissor(scissor);
		}
	}

	void GraphicsCommandList::BeginRendering(const RenderingInfo& renderingInfo)
	{
		CORE_ASSERT(renderingInfo.Width > 0 && renderingInfo.Height > 0, "BeginRendering received an empty render area");

		m_ActiveColorAttachments.clear();
		m_ActiveDepthAttachment.reset();

		// The legacy VkRenderPass provided initial/final layout transitions via
		// subpass dependencies. Dynamic Rendering has no equivalent, so every
		// attachment is transitioned explicitly here and in EndRendering.
		std::vector<VkRenderingAttachmentInfo> colorAttachments;
		colorAttachments.reserve(renderingInfo.ColorAttachments.size());
		for (const RenderingAttachmentInfo& colorAttachmentInfo : renderingInfo.ColorAttachments)
		{
			RenderTargetTexture* target = ResourceManager::GetRenderTarget(colorAttachmentInfo.Target);
			CORE_ASSERT(target, "BeginRendering received an invalid color attachment");
			Image* image = ResourceManager::GetImage(target->GetImage());
			CORE_ASSERT(image, "Color attachment render target has no image");
			ImageView* imageView = ResourceManager::GetImageView(target->GetImageView());
			CORE_ASSERT(imageView, "Color attachment render target has no image view");

			// The previous state may be either another frame's shader-read (after
			// EndRendering transitioned it) or, within the same frame, another
			// pass's attachment write reused via LOAD_OP::LOAD. Cover both so the
			// barrier is correct regardless of which produced the current contents.
			image->InsertImageMemoryBarrier(
				*this,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				ColorSubresourceRange());

			VkRenderingAttachmentInfo& attachment = colorAttachments.emplace_back();
			attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			attachment.imageView = imageView->GetHandle();
			attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment.resolveMode = VK_RESOLVE_MODE_NONE;
			attachment.loadOp = VulkanLoadOp(colorAttachmentInfo.LoadOp);
			attachment.storeOp = VulkanStoreOp(colorAttachmentInfo.StoreOp);
			const glm::vec4 clearColor = target->GetClearColor();
			attachment.clearValue.color = { { clearColor.r, clearColor.g, clearColor.b, clearColor.a } };

			m_ActiveColorAttachments.push_back(colorAttachmentInfo.Target);
		}

		VkRenderingAttachmentInfo depthAttachment{};
		const bool hasDepthAttachment = renderingInfo.DepthAttachment.has_value();
		if (hasDepthAttachment)
		{
			const RenderingAttachmentInfo& depthAttachmentInfo = *renderingInfo.DepthAttachment;
			RenderTargetTexture* target = ResourceManager::GetRenderTarget(depthAttachmentInfo.Target);
			CORE_ASSERT(target, "BeginRendering received an invalid depth attachment");
			Image* image = ResourceManager::GetImage(target->GetImage());
			CORE_ASSERT(image, "Depth attachment render target has no image");
			ImageView* imageView = ResourceManager::GetImageView(target->GetImageView());
			CORE_ASSERT(imageView, "Depth attachment render target has no image view");

			// See the color-attachment barrier above: the previous writer may be a
			// prior frame (shader-read) or, within the same frame, an earlier pass
			// such as PreDepth writing the same SceneDepth via LOAD_OP::LOAD.
			image->InsertImageMemoryBarrier(
				*this,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				DepthSubresourceRange());

			depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			depthAttachment.imageView = imageView->GetHandle();
			depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
			depthAttachment.loadOp = VulkanLoadOp(depthAttachmentInfo.LoadOp);
			depthAttachment.storeOp = VulkanStoreOp(depthAttachmentInfo.StoreOp);
			// Matches the depth clear the Framebuffer wrapper used unconditionally.
			depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

			m_ActiveDepthAttachment = depthAttachmentInfo.Target;
		}

		VkRenderingInfo vulkanRenderingInfo{};
		vulkanRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		vulkanRenderingInfo.renderArea.offset = { 0, 0 };
		vulkanRenderingInfo.renderArea.extent = { renderingInfo.Width, renderingInfo.Height };
		vulkanRenderingInfo.layerCount = 1;
		vulkanRenderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
		vulkanRenderingInfo.pColorAttachments = colorAttachments.data();
		vulkanRenderingInfo.pDepthAttachment = hasDepthAttachment ? &depthAttachment : nullptr;

		vkCmdBeginRendering(GetNative(), &vulkanRenderingInfo);

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(renderingInfo.Width);
		viewport.height = static_cast<float>(renderingInfo.Height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		SetViewport(viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = { renderingInfo.Width, renderingInfo.Height };
		SetScissor(scissor);
	}

	void GraphicsCommandList::EndRendering()
	{
		vkCmdEndRendering(GetNative());

		// The legacy VkRenderPass exposed attachments that were sampled elsewhere
		// (e.g. the editor viewport) in VK_IMAGE_LAYOUT_*_READ_ONLY_OPTIMAL as its
		// finalLayout. Reproduce that here, driven by the same RenderTargetTexture
		// ShaderResource flag that already controls whether a sampler exists for it.
		for (Handle<RenderTargetTexture> colorAttachment : m_ActiveColorAttachments)
		{
			RenderTargetTexture* target = ResourceManager::GetRenderTarget(colorAttachment);
			if (!target || !target->IsShaderResource())
				continue;

			Image* image = ResourceManager::GetImage(target->GetImage());
			CORE_ASSERT(image, "Color attachment render target has no image");
			image->InsertImageMemoryBarrier(
				*this,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				ColorSubresourceRange());
		}

		if (m_ActiveDepthAttachment.has_value())
		{
			RenderTargetTexture* target = ResourceManager::GetRenderTarget(*m_ActiveDepthAttachment);
			if (target && target->IsShaderResource())
			{
				Image* image = ResourceManager::GetImage(target->GetImage());
				CORE_ASSERT(image, "Depth attachment render target has no image");
				image->InsertImageMemoryBarrier(
					*this,
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					DepthSubresourceRange());
			}
		}

		m_ActiveColorAttachments.clear();
		m_ActiveDepthAttachment.reset();
	}

	void GraphicsCommandList::BeginRenderingAttachments(const RenderingInfo& renderingInfo)
	{
		CORE_ASSERT(renderingInfo.Width > 0 && renderingInfo.Height > 0, "BeginRenderingAttachments received an empty render area");

		std::vector<VkRenderingAttachmentInfo> colorAttachments;
		colorAttachments.reserve(renderingInfo.ColorAttachments.size());
		for (const RenderingAttachmentInfo& colorAttachmentInfo : renderingInfo.ColorAttachments)
			colorAttachments.push_back(BuildColorAttachmentInfo(colorAttachmentInfo));

		VkRenderingAttachmentInfo depthAttachment{};
		const bool hasDepthAttachment = renderingInfo.DepthAttachment.has_value();
		if (hasDepthAttachment)
			depthAttachment = BuildDepthAttachmentInfo(*renderingInfo.DepthAttachment);

		VkRenderingInfo vulkanRenderingInfo{};
		vulkanRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		vulkanRenderingInfo.renderArea.offset = { 0, 0 };
		vulkanRenderingInfo.renderArea.extent = { renderingInfo.Width, renderingInfo.Height };
		vulkanRenderingInfo.layerCount = 1;
		vulkanRenderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
		vulkanRenderingInfo.pColorAttachments = colorAttachments.data();
		vulkanRenderingInfo.pDepthAttachment = hasDepthAttachment ? &depthAttachment : nullptr;

		vkCmdBeginRendering(GetNative(), &vulkanRenderingInfo);
		SetViewportAndScissor(*this, renderingInfo.Width, renderingInfo.Height);
	}

	void GraphicsCommandList::EndRenderingAttachments()
	{
		vkCmdEndRendering(GetNative());
	}

	void GraphicsCommandList::BindGraphicsPipeline(Handle<GraphicsPipeline> pipelineHandle)
	{
		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(pipelineHandle);
		CORE_ASSERT(pipeline, "Invalid graphics pipeline");

		vkCmdBindPipeline(GetNative(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());
		m_BoundPipelineLayout = pipeline->GetVulkanPipelineLayout();
	}

	void GraphicsCommandList::PushConstants(
		VkShaderStageFlags shaderStages,
		std::span<const std::byte> data,
		uint32_t offset)
	{
		CORE_ASSERT(m_BoundPipelineLayout != VK_NULL_HANDLE, "PushConstants requires a bound pipeline");
		CommandListBase::PushConstants(m_BoundPipelineLayout, shaderStages, data, offset);
	}

	void GraphicsCommandList::BindVertexBuffer(
		Handle<GPUBuffer> vertexBufferHandle,
		uint32_t binding,
		VkDeviceSize offset)
	{
		GPUBuffer* vertexBuffer = ResourceManager::GetBuffer(vertexBufferHandle);
		CORE_ASSERT(vertexBuffer, "Invalid vertex buffer");
		vkCmdBindVertexBuffers(GetNative(), binding, 1, &vertexBuffer->Buffer, &offset);
	}

	void GraphicsCommandList::BindIndexBuffer(
		Handle<GPUBuffer> indexBufferHandle,
		VkDeviceSize offset,
		VkIndexType indexType)
	{
		GPUBuffer* indexBuffer = ResourceManager::GetBuffer(indexBufferHandle);
		CORE_ASSERT(indexBuffer, "Invalid index buffer");
		vkCmdBindIndexBuffer(GetNative(), indexBuffer->Buffer, offset, indexType);
	}

	void GraphicsCommandList::BindDescriptorSets(Handle<BindGroup> bindGroupHandle, uint32_t firstSet)
	{
		BindDescriptorSetInternal(
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_BoundPipelineLayout,
			bindGroupHandle,
			firstSet);
	}

	void GraphicsCommandList::BindDescriptorSets(
		std::span<const Handle<BindGroup>> bindGroups,
		uint32_t firstSet)
	{
		BindDescriptorSetsInternal(
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_BoundPipelineLayout,
			bindGroups,
			firstSet);
	}

	void GraphicsCommandList::BindDescriptorSets(
		std::span<const VkDescriptorSet> descriptorSets,
		uint32_t firstSet)
	{
		BindDescriptorSetsInternal(
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_BoundPipelineLayout,
			descriptorSets,
			firstSet);
	}

	void GraphicsCommandList::SetViewport(const VkViewport& viewport)
	{
		vkCmdSetViewport(GetNative(), 0, 1, &viewport);
	}

	void GraphicsCommandList::SetScissor(const VkRect2D& scissor)
	{
		vkCmdSetScissor(GetNative(), 0, 1, &scissor);
	}

	void GraphicsCommandList::Draw(
		uint32_t vertexCount,
		uint32_t instanceCount,
		uint32_t firstVertex,
		uint32_t firstInstance)
	{
		vkCmdDraw(GetNative(), vertexCount, instanceCount, firstVertex, firstInstance);
		if (RendererFrameStatistics* statistics = GetStatistics())
			++statistics->DrawCallCount;
	}

	void GraphicsCommandList::DrawIndexed(
		uint32_t indexCount,
		uint32_t instanceCount,
		uint32_t firstIndex,
		int32_t vertexOffset,
		uint32_t firstInstance)
	{
		vkCmdDrawIndexed(GetNative(), indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
		if (RendererFrameStatistics* statistics = GetStatistics())
			++statistics->DrawCallCount;
	}

	void GraphicsCommandList::DrawFullscreen(
		Handle<GraphicsPipeline> pipeline,
		Handle<BindGroup> bindGroup)
	{
		BindGraphicsPipeline(pipeline);
		if (bindGroup.IsValid())
			BindDescriptorSets(bindGroup, 0);
		Draw(3);
	}

	void GraphicsCommandList::ExecuteDrawStream(
		const DrawArea& drawArea,
		std::span<const uint32_t> drawStream)
	{
		CORE_ASSERT(drawArea.DrawOffset <= drawStream.size(), "Draw stream offset is out of bounds");
		BindGraphicsPipeline(drawArea.GraphicsPipeline);
		if (drawArea.BindGroup0.IsValid())
			BindDescriptorSets(drawArea.BindGroup0, 0);
		if (drawArea.BindGroup1.IsValid())
			BindDescriptorSets(drawArea.BindGroup1, 1);

		DrawStreamReader stream(drawStream.subspan(drawArea.DrawOffset));
		DrawCallInfo draw{};
		for (uint32_t drawIndex = 0; drawIndex < drawArea.DrawCount; ++drawIndex)
		{
			const uint32_t dirtyFlags = stream.Read<uint32_t>();
			if (dirtyFlags & DrawStreamFlags::VERTEX_BUFFER)
			{
				draw.VertexBuffer = stream.Read<Handle<GPUBuffer>>();
				BindVertexBuffer(draw.VertexBuffer);
			}

			if (dirtyFlags & DrawStreamFlags::INDEX_BUFFER)
			{
				draw.IndexBuffer = stream.Read<Handle<GPUBuffer>>();
				BindIndexBuffer(draw.IndexBuffer);
			}

			if (dirtyFlags & DrawStreamFlags::VERTEX_OFFSET)
				draw.VertexOffset = stream.Read<uint32_t>();
			if (dirtyFlags & DrawStreamFlags::INDEX_OFFSET)
				draw.IndexOffset = stream.Read<uint32_t>();
			if (dirtyFlags & DrawStreamFlags::INDEX_COUNT)
				draw.IndexCount = stream.Read<uint32_t>();
			if (dirtyFlags & DrawStreamFlags::WORLD_TRANSFORM)
				draw.WorldTransform = stream.Read<glm::mat4>();
			if (dirtyFlags & DrawStreamFlags::MATERIAL_INDEX)
				draw.MaterialIndex = stream.Read<uint32_t>();

			CORE_ASSERT(draw.VertexBuffer.IsValid(), "Draw stream has no vertex-buffer state");
			CORE_ASSERT(draw.IndexBuffer.IsValid(), "Draw stream has no index-buffer state");
			CORE_ASSERT(draw.IndexCount > 0, "Draw stream has no index-count state");

			// Every DrawStream-consuming vertex shader declares this same
			// push_constant block shape (see the ExecuteDrawStream declaration), so
			// this push is valid against whichever pipeline is currently bound.
			DrawStreamPushConstants pushConstants;
			pushConstants.WorldTransform = draw.WorldTransform;
			pushConstants.MaterialIndex = draw.MaterialIndex;
			PushConstants(
				m_BoundPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT,
				AsBytes(pushConstants));
			DrawIndexed(draw.IndexCount, 1, draw.IndexOffset, static_cast<int32_t>(draw.VertexOffset), 0);
		}
	}
}
