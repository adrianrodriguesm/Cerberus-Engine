#pragma once

#include "Core/Handle.h"
#include "Renderer/RHI/Command/CommandList.h"
#include "Renderer/RHI/RenderingInfo.h"

#include <functional>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace cerberus
{
	class CommandBuffer;
	class ComputeCommandList;
	class GraphicsCommandList;
	class Image;
	class RayTracingCommandList;
	class RenderTargetTexture;

	// Per-graph resource handle. Distinct from Handle<RenderTargetTexture>/
	// Handle<Image>: the graph tracks its own per-frame access history keyed by
	// these, separate from ResourceManager's generational pools. Only valid for
	// the RenderGraph instance and frame that produced it.
	struct RGTextureHandle
	{
		static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
		uint32_t Index = InvalidIndex;
		bool IsValid() const { return Index != InvalidIndex; }
	};

	// Synchronization scope produced by an access: what memory became visible,
	// from which pipeline stage(s). Image layout is not tracked here -- it is
	// tracked by Image itself (see Image::InsertImageMemoryBarrier), the same
	// way non-graph code already relies on it.
	struct RGAccessState
	{
		VkAccessFlags Access = 0;
		VkPipelineStageFlags Stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	};

	enum class RGAccessKind : uint8_t
	{
		ReadTexture,
		WriteColorAttachment,
		WriteDepthAttachment,
		ReadWriteStorageImage
	};

	// One pass's declared use of one imported resource. LoadOp/StoreOp only
	// apply to the two WriteXAttachment kinds; Compile() fills Before/After once
	// the pass execution order and per-resource access history are known.
	struct RGResourceAccess
	{
		RGTextureHandle Resource;
		RGAccessKind Kind = RGAccessKind::ReadTexture;
		VkPipelineStageFlags Stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		LOAD_OP LoadOp = LOAD_OP::DONT_CARE;
		STORE_OP StoreOp = STORE_OP::DONT_CARE;

		RGAccessState Before{};
		RGAccessState After{};
	};

	class RenderGraph;

	// Passed to a pass's setup callback. Declares which already-imported
	// resources the pass touches and how; Compile() derives pass ordering and
	// barriers from these declarations instead of a call site hand-writing
	// either. Resources are imported once per frame on RenderGraph itself (see
	// RenderGraph::ImportRenderTarget/ImportImage), not through this builder --
	// a builder instance is scoped to one pass's setup call, and the same
	// underlying resource is typically read/written by several passes, so
	// importing it per-pass would hand out a distinct handle to each and break
	// dependency tracking between them.
	class RenderGraphBuilder
	{
	public:
		void ReadTexture(RGTextureHandle handle, VkPipelineStageFlags stages);
		void WriteColorAttachment(RGTextureHandle handle, LOAD_OP loadOp, STORE_OP storeOp);
		void WriteDepthAttachment(RGTextureHandle handle, LOAD_OP loadOp, STORE_OP storeOp);
		void ReadWriteStorageImage(RGTextureHandle handle, VkPipelineStageFlags stages);

	private:
		friend class RenderGraph;
		RenderGraphBuilder(RenderGraph& graph, uint32_t passIndex)
			: m_Graph(graph), m_PassIndex(passIndex)
		{
		}

		RenderGraph& m_Graph;
		uint32_t m_PassIndex;
	};

	using RGSetupFn = std::function<void(RenderGraphBuilder&)>;
	using RGGraphicsExecuteFn = std::function<void(GraphicsCommandList&)>;
	using RGComputeExecuteFn = std::function<void(ComputeCommandList&)>;
	using RGRayTracingExecuteFn = std::function<void(RayTracingCommandList&)>;

	// Increment 1: pass ordering and precise, per-resource barrier computation
	// from declared reads/writes. Every imported resource is externally owned
	// (SceneRenderer keeps its Handle<RenderTargetTexture>/Handle<Image> members
	// exactly as before) -- transient/aliased resource allocation is a later,
	// separate increment. One RenderGraph is kept alive per SceneRenderer and
	// Reset() between frames rather than reconstructed.
	class RenderGraph
	{
	public:
		// Registers an externally-owned resource with this frame's graph. Call
		// once per resource per frame, before adding any pass that reads/writes
		// it; every pass that touches the same underlying resource must use the
		// same returned handle (see the RenderGraphBuilder comment above).
		RGTextureHandle ImportRenderTarget(Handle<RenderTargetTexture> target, const char* name);
		// initialAccess describes what the resource is assumed to already be in
		// when this frame's graph first touches it (e.g. a persistent storage
		// image left mid-read/write by the previous frame's dispatch). Render
		// targets infer this from their attachment kind instead -- see
		// ImportRenderTarget.
		RGTextureHandle ImportImage(Handle<Image> image, const char* name, RGAccessState initialAccess);

		void AddGraphicsPass(const char* name, RGSetupFn setup, RGGraphicsExecuteFn execute);
		void AddComputePass(const char* name, RGSetupFn setup, RGComputeExecuteFn execute);
		void AddRayTracingPass(const char* name, RGSetupFn setup, RGRayTracingExecuteFn execute);

		// Topo-sorts passes from their resource dependencies (a pass depends on
		// every earlier pass that wrote a resource it reads or writes) and
		// resolves each access's exact Before/After synchronization scope by
		// walking resources in that order. Ties are broken by registration
		// order, so passes with no cross-dependency (e.g. the shadow cascades,
		// which each write a different target) keep the order they were added.
		void Compile();

		// Records every pass's barriers, then its execute callback, into
		// commandBuffer's current recording, in compiled order -- still one
		// BeginRenderThread/EndRenderThread/SubmitRenderThread per frame at the
		// call site, exactly like before the graph existed. Resources flagged
		// shader-readable that no pass reads back (e.g. the final composite
		// output the editor samples outside this graph) get one closing
		// transition to a shader-read layout after the last pass, mirroring
		// what GraphicsCommandList::EndRendering used to do unconditionally.
		void Execute(CommandBuffer& commandBuffer, uint32_t frameIndex, RendererFrameStatistics* statistics);

		// Clears the pass/resource lists so the graph can be rebuilt next frame.
		void Reset();

		// Public so RenderGraph.cpp's barrier-resolution helpers (free functions,
		// not members) can read it; not meant to be constructed outside RenderGraph.
		struct Resource
		{
			bool IsImage = false; // false = RenderTargetTexture, true = plain Image
			Handle<RenderTargetTexture> RenderTarget;
			Handle<Image> ImageHandle;
			std::string Name;
			bool IsDepthFormat = false;
			bool IsShaderResource = false;
			RGAccessState InitialState{};
			bool NeedsExportTransition = false;
			RGAccessState FinalState{};
		};

	private:
		friend class RenderGraphBuilder;

		struct Pass
		{
			std::string Name;
			enum class Type : uint8_t { Graphics, Compute, RayTracing } Type = Type::Graphics;
			std::vector<RGResourceAccess> Accesses;
			RGGraphicsExecuteFn GraphicsExecute;
			RGComputeExecuteFn ComputeExecute;
			RGRayTracingExecuteFn RayTracingExecute;
			std::vector<uint32_t> DependsOnPasses;
		};

		void RecordAccess(uint32_t passIndex, RGResourceAccess access);

		std::vector<Resource> m_Resources;
		std::vector<Pass> m_Passes;
		std::vector<uint32_t> m_ExecutionOrder;
		bool m_Compiled = false;
	};
}
