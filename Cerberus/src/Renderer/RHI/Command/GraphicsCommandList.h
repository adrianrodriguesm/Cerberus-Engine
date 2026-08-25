#pragma once

#include "Renderer/RHI/Command/CommandList.h"
#include "Renderer/DrawStreamBuilder.h"
#include "Renderer/RHI/RenderingInfo.h"

namespace cerberus
{
	// Typed graphics recording view. It borrows a command buffer and never submits it.
	class GraphicsCommandList final : public CommandListBase
	{
	public:
		GraphicsCommandList(
			CommandBufferView commandBuffer,
			RendererFrameStatistics* statistics = nullptr);

		// Opens one Dynamic Rendering scope (vkCmdBeginRendering). Transitions every
		// attachment into its attachment-optimal layout first. A logical pass (Sky,
		// Forward, ...) does not need its own scope; several passes may record
		// between one BeginRendering/EndRendering pair.
		void BeginRendering(const RenderingInfo& renderingInfo);
		// Closes the scope (vkCmdEndRendering) and transitions every attachment
		// whose RenderTargetTexture is a shader resource into a shader-read layout,
		// mirroring the finalLayout behaviour the legacy VkRenderPass used to provide.
		void EndRendering();

		void BindGraphicsPipeline(Handle<GraphicsPipeline> pipeline);
		void BindVertexBuffer(
			Handle<GPUBuffer> vertexBuffer,
			uint32_t binding = 0,
			VkDeviceSize offset = 0);
		void BindIndexBuffer(
			Handle<GPUBuffer> indexBuffer,
			VkDeviceSize offset = 0,
			VkIndexType indexType = VK_INDEX_TYPE_UINT32);
		void BindDescriptorSets(Handle<BindGroup> bindGroup, uint32_t firstSet = 0);
		void BindDescriptorSets(
			std::span<const Handle<BindGroup>> bindGroups,
			uint32_t firstSet = 0);
		void BindDescriptorSets(
			std::span<const VkDescriptorSet> descriptorSets,
			uint32_t firstSet = 0);

		void SetViewport(const VkViewport& viewport);
		void SetScissor(const VkRect2D& scissor);

		void Draw(
			uint32_t vertexCount,
			uint32_t instanceCount = 1,
			uint32_t firstVertex = 0,
			uint32_t firstInstance = 0);
		void DrawIndexed(
			uint32_t indexCount,
			uint32_t instanceCount = 1,
			uint32_t firstIndex = 0,
			int32_t vertexOffset = 0,
			uint32_t firstInstance = 0);

		void DrawFullscreen(Handle<GraphicsPipeline> pipeline, Handle<BindGroup> bindGroup = {});
		void ExecuteDrawStream(const DrawArea& drawArea, std::span<const uint32_t> drawStream);
		void RenderMeshInstances(
			const DrawArea& drawArea,
			std::span<const Handle<MeshInstanceProxy>> meshInstances,
			std::span<const MaterialProxy> materialProxies);

	private:
		VkPipelineLayout m_BoundPipelineLayout = VK_NULL_HANDLE;

		// Attachments of the currently open rendering scope, kept only to drive the
		// post-render layout transition performed by EndRendering.
		std::vector<Handle<RenderTargetTexture>> m_ActiveColorAttachments;
		std::optional<Handle<RenderTargetTexture>> m_ActiveDepthAttachment;
	};
}
