#pragma once

#include "Renderer/RHI/Command/CommandList.h"
#include "Renderer/Draw/DrawStreamBuilder.h"
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

		// Same vkCmdBeginRendering/vkCmdEndRendering pair as BeginRendering/
		// EndRendering, without inserting any barrier: the caller (RenderGraph)
		// has already recorded the exact barrier it computed for every
		// attachment beforehand, from real per-frame access history rather than
		// BeginRendering's conservative guess. Only RenderGraph::Execute calls
		// these; anything not recorded through the graph should keep using
		// BeginRendering/EndRendering.
		void BeginRenderingAttachments(const RenderingInfo& renderingInfo);
		void EndRenderingAttachments();

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

		// args must hold IndirectDrawCommand entries (IndirectBufferUsage) --
		// one vkCmdDrawIndexedIndirect for drawCount commands starting at
		// offset. stride defaults to sizeof(IndirectDrawCommand) (tightly
		// packed); pass a larger stride only if args interleaves other data
		// between commands.
		void DrawIndexedIndirect(
			Handle<GPUBuffer> args,
			uint64_t offset,
			uint32_t drawCount,
			uint32_t stride = sizeof(IndirectDrawCommand));
		// Same as above, but the actual draw count is read from count (a
		// single uint32_t at countOffset) at execution time, capped at
		// maxDrawCount -- for when the count itself is GPU-written (week 2 /
		// T5 GPU culling). In T4 count is just a CPU-filled upper bound.
		void DrawIndexedIndirectCount(
			Handle<GPUBuffer> args,
			uint64_t offset,
			Handle<GPUBuffer> count,
			uint64_t countOffset,
			uint32_t maxDrawCount,
			uint32_t stride = sizeof(IndirectDrawCommand));
		// Replays a stream encoded by DrawStreamBuilder, pushing { WorldTransform,
		// MaterialIndex } once per draw. Every vertex shader that consumes a
		// DrawStream (PreDepth, Shadow, Forward) declares the identical
		// push_constant block shape, even where a stage ignores MaterialIndex, so
		// pipeline-layout reflection produces the same range everywhere and this
		// can push unconditionally without special-casing per pipeline.
		void ExecuteDrawStream(const DrawArea& drawArea, std::span<const uint32_t> drawStream);

		// Same binding + push-constant setup as ExecuteDrawStream, but issues
		// one vkCmdDrawIndexedIndirect instead of replaying a CPU-side
		// stream. args must hold drawCount IndirectDrawCommand entries
		// (RenderScene::BuildIndirectArgs), each already carrying its own
		// firstInstance -- see docs/tasks/T4-instances-indirect.md.
		void ExecuteDrawIndirect(const DrawArea& drawArea, Handle<GPUBuffer> args, uint32_t drawCount);

		// Pushes against the currently bound pipeline's layout (set by the most
		// recent BindGraphicsPipeline/ExecuteDrawStream call). Lets a pass push
		// data beyond the shared DrawStream shape, e.g. ShadowPass pushing one
		// cascade's light-space matrix once per cascade rather than per draw.
		using CommandListBase::PushConstants;
		void PushConstants(
			VkShaderStageFlags shaderStages,
			std::span<const std::byte> data,
			uint32_t offset = 0);

	private:
		VkPipelineLayout m_BoundPipelineLayout = VK_NULL_HANDLE;

		// Attachments of the currently open rendering scope, kept only to drive the
		// post-render layout transition performed by EndRendering.
		std::vector<Handle<RenderTargetTexture>> m_ActiveColorAttachments;
		std::optional<Handle<RenderTargetTexture>> m_ActiveDepthAttachment;
	};
}
