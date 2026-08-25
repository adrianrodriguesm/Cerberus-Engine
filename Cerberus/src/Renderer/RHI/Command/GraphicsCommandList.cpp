#include "pch.h"
#include "GraphicsCommandList.h"

#include "Renderer/Core/RendererContext.h"
#include "Renderer/RHI/ResourceManager.h"

#include <cmath>

namespace cerberus
{
	namespace
	{
		// One vertex-stage range keeps the current Shader push-constant reflection
		// deterministic. The material index is forwarded flat to the fragment shader.
		struct RasterDrawPushConstants
		{
			glm::mat4 WorldTransform = glm::mat4(1.0f);
			glm::vec4 NormalColumn0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
			glm::vec4 NormalColumn1 = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
			glm::vec4 NormalColumn2 = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
			uint32_t MaterialIndex = 0;
		};

		static_assert(sizeof(RasterDrawPushConstants) == 116,
			"RasterDrawPushConstants must match ForwardPBR.vert");
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

			image->InsertImageMemoryBarrier(
				*this,
				0,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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

			image->InsertImageMemoryBarrier(
				*this,
				0,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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

	void GraphicsCommandList::BindGraphicsPipeline(Handle<GraphicsPipeline> pipelineHandle)
	{
		GraphicsPipeline* pipeline = ResourceManager::GetGraphicsPipeline(pipelineHandle);
		CORE_ASSERT(pipeline, "Invalid graphics pipeline");

		vkCmdBindPipeline(GetNative(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());
		m_BoundPipelineLayout = pipeline->GetVulkanPipelineLayout();
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
			if (dirtyFlags & DrawStreamFlags::BIND_GROUP_MAT)
			{
				draw.MaterialBindGroup = stream.Read<Handle<BindGroup>>();
				BindDescriptorSets(draw.MaterialBindGroup, drawArea.MaterialBindGroupSet);
			}

			CORE_ASSERT(draw.VertexBuffer.IsValid(), "Draw stream has no vertex-buffer state");
			CORE_ASSERT(draw.IndexBuffer.IsValid(), "Draw stream has no index-buffer state");
			CORE_ASSERT(draw.IndexCount > 0, "Draw stream has no index-count state");
			DrawIndexed(draw.IndexCount, 1, draw.IndexOffset, static_cast<int32_t>(draw.VertexOffset), 0);
		}
	}

	void GraphicsCommandList::RenderMeshInstances(
		const DrawArea& drawArea,
		std::span<const Handle<MeshInstanceProxy>> meshInstances,
		std::span<const MaterialProxy> materialProxies)
	{
		if (meshInstances.empty())
			return;

		BindGraphicsPipeline(drawArea.GraphicsPipeline);
		if (drawArea.BindGroup0.IsValid())
			BindDescriptorSets(drawArea.BindGroup0, 0);
		if (drawArea.BindGroup1.IsValid())
			BindDescriptorSets(drawArea.BindGroup1, 1);
		Handle<MeshSourceProxy> activeMeshSource;

		for (Handle<MeshInstanceProxy> meshInstanceHandle : meshInstances)
		{
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstanceHandle);
			CORE_ASSERT(meshInstance, "Invalid MeshInstanceProxy");

			MeshSourceProxy* meshSource = ResourceManager::GetMeshSourceProxy(meshInstance->MeshSource);
			CORE_ASSERT(meshSource, "MeshInstanceProxy references an invalid MeshSourceProxy");
			CORE_ASSERT(meshSource->VertexBuffer && meshSource->IndexBuffer, "MeshSourceProxy has invalid geometry buffers");
			CORE_ASSERT(meshInstance->SubmeshIndex < meshSource->Submeshes.size(), "MeshInstanceProxy references an invalid submesh");

			if (meshInstance->MeshSource != activeMeshSource)
			{
				BindVertexBuffer(meshSource->VertexBuffer->GetGPUBuffer());
				BindIndexBuffer(meshSource->IndexBuffer->GetGPUBuffer());
				activeMeshSource = meshInstance->MeshSource;
			}

			const SubmeshSourceProxy& submesh = meshSource->Submeshes[meshInstance->SubmeshIndex];
			CORE_ASSERT(meshInstance->MaterialProxyIndex < materialProxies.size(), "MeshInstanceProxy references an invalid raster MaterialProxy");
			const MaterialProxy& materialProxy = materialProxies[meshInstance->MaterialProxyIndex];
			CORE_ASSERT(materialProxy.GPUMaterialIndex != InvalidMaterialProxyIndex, "Raster MaterialProxy has no GPU index");
			RasterDrawPushConstants pushConstants;
			pushConstants.WorldTransform = meshInstance->WorldTransform;
			const glm::mat3 worldTransform3x3(meshInstance->WorldTransform);
			const float determinant = glm::determinant(worldTransform3x3);
			const glm::mat3 normalTransform = std::abs(determinant) > 0.000001f
				? glm::transpose(glm::inverse(worldTransform3x3))
				: worldTransform3x3;
			pushConstants.NormalColumn0 = glm::vec4(normalTransform[0], 0.0f);
			pushConstants.NormalColumn1 = glm::vec4(normalTransform[1], 0.0f);
			pushConstants.NormalColumn2 = glm::vec4(normalTransform[2], 0.0f);
			pushConstants.MaterialIndex = materialProxy.GPUMaterialIndex;
			PushConstants(
				m_BoundPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT,
				AsBytes(pushConstants));
			DrawIndexed(submesh.IndexCount, 1, submesh.BaseIndex, submesh.BaseVertex, 0);
		}
	}
}
