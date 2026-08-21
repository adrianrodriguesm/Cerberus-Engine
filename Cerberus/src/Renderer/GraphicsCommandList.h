#pragma once

#include "Renderer/CommandList.h"
#include "Renderer/DrawStreamBuilder.h"

namespace cerberus
{
	// Typed graphics recording view. It borrows a command buffer and never submits it.
	class GraphicsCommandList final : public CommandListBase
	{
	public:
		GraphicsCommandList(
			CommandBufferView commandBuffer,
			RendererFrameStatistics* statistics = nullptr);

		void BeginRendering(
			Handle<RenderPass> renderPass,
			Handle<Framebuffer> framebuffer,
			bool explicitClear = false);
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
			std::span<const Handle<MeshInstanceProxy>> meshInstances);

	private:
		VkPipelineLayout m_BoundPipelineLayout = VK_NULL_HANDLE;
	};
}
