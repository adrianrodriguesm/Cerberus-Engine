#include "pch.h"
#include "GraphicsCommandList.h"

#include "Renderer/RendererContext.h"
#include "Renderer/ResourceManager.h"

namespace cerberus
{
	GraphicsCommandList::GraphicsCommandList(
		CommandBufferView commandBuffer,
		RendererFrameStatistics* statistics)
		: CommandListBase(commandBuffer, statistics)
	{
	}

	void GraphicsCommandList::BeginRendering(
		Handle<RenderPass> renderPassHandle,
		Handle<Framebuffer> framebufferHandle,
		bool explicitClear)
	{
		Framebuffer* framebuffer = ResourceManager::GetFramebuffer(framebufferHandle);
		RenderPass* renderPass = ResourceManager::GetRenderPass(renderPassHandle);
		CORE_ASSERT(framebuffer, "BeginRendering received an invalid framebuffer");
		CORE_ASSERT(renderPass, "BeginRendering received an invalid render pass");

		const uint32_t width = framebuffer->GetWidth();
		const uint32_t height = framebuffer->GetHeight();
		const std::vector<VkClearValue>& clearValues = framebuffer->GetVulkanClearValues();

		VkRenderPassBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		beginInfo.renderPass = renderPass->GetHandle();
		beginInfo.framebuffer = framebuffer->GetHandle();
		beginInfo.renderArea.offset = { 0, 0 };
		beginInfo.renderArea.extent = { width, height };
		beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		beginInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(GetNative(), &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

		if (explicitClear)
		{
			const uint32_t colorAttachmentCount = static_cast<uint32_t>(framebuffer->GetColorAttachmentCount());
			const uint32_t attachmentCount = colorAttachmentCount + (framebuffer->HasDepthAttachment() ? 1u : 0u);
			CORE_ASSERT(clearValues.size() == attachmentCount, "Framebuffer clear values do not match its attachments");

			std::vector<VkClearAttachment> attachments(attachmentCount);
			for (uint32_t attachmentIndex = 0; attachmentIndex < colorAttachmentCount; ++attachmentIndex)
			{
				attachments[attachmentIndex].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				attachments[attachmentIndex].colorAttachment = attachmentIndex;
				attachments[attachmentIndex].clearValue = clearValues[attachmentIndex];
			}

			if (framebuffer->HasDepthAttachment())
			{
				RenderTargetTexture* depthTarget = ResourceManager::GetRenderTarget(framebuffer->GetDepthTarget());
				CORE_ASSERT(depthTarget, "Framebuffer has an invalid depth attachment");
				attachments[colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				const VkFormat depthFormat = depthTarget->GetFormat() == ImageFormat::DEPTH32F
					? VK_FORMAT_D32_SFLOAT
					: RendererContext::GetCurrentDevice()->GetPhysicalDevice()->GetDepthFormat();
				if (depthFormat == VK_FORMAT_D16_UNORM_S8_UINT
					|| depthFormat == VK_FORMAT_D24_UNORM_S8_UINT
					|| depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
					attachments[colorAttachmentCount].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
				attachments[colorAttachmentCount].clearValue = clearValues[colorAttachmentCount];
			}

			VkClearRect clearRect{};
			clearRect.rect.offset = { 0, 0 };
			clearRect.rect.extent = { width, height };
			clearRect.baseArrayLayer = 0;
			clearRect.layerCount = 1;

			vkCmdClearAttachments(
				GetNative(),
				attachmentCount,
				attachments.data(),
				1,
				&clearRect);
		}

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(width);
		viewport.height = static_cast<float>(height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		SetViewport(viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = { width, height };
		SetScissor(scissor);
	}

	void GraphicsCommandList::EndRendering()
	{
		vkCmdEndRenderPass(GetNative());
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
		std::span<const Handle<MeshInstanceProxy>> meshInstances)
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
			PushConstants(
				m_BoundPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT,
				AsBytes(meshInstance->WorldTransform));
			DrawIndexed(submesh.IndexCount, 1, submesh.BaseIndex, submesh.BaseVertex, 0);
		}
	}
}
