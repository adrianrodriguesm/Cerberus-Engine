#pragma once

#include "Core/Handle.h"

#include <cstdint>
#include <span>

namespace cerberus
{
	class BindGroup;
	struct GPUBuffer;
	class GraphicsCommandList;
	class GraphicsPipeline;

	struct PreDepthPassResources
	{
		Handle<GraphicsPipeline> Pipeline;
		Handle<BindGroup> BindGroup;
	};

	struct PreDepthPassParameters
	{
		std::span<const uint32_t> DrawStream;
		uint32_t DrawCount = 0;
		// GeometryPool vertex buffer address, for vertex pulling -- see
		// docs/tasks/T3-geometry-pool.md.
		uint64_t VertexBufferAddress = 0;
		// GPUInstanceData buffer address, read via Instances[gl_InstanceIndex]
		// -- see docs/tasks/T4-instances-indirect.md.
		uint64_t InstanceBufferAddress = 0;
		// GeometryPool index buffer -- bound directly for the Indirect path
		// (ExecuteDrawStream binds it from the stream instead, for Direct).
		Handle<GPUBuffer> IndexBuffer;

		// Direct (DrawStream replay) vs Indirect (one DrawIndexedIndirect) --
		// see docs/tasks/T4-instances-indirect.md. IndirectArgs/
		// IndirectDrawCount are only read when UseIndirect is true.
		bool UseIndirect = false;
		Handle<GPUBuffer> IndirectArgs;
		uint32_t IndirectDrawCount = 0;
	};

	// Depth-only pass writing SceneDepth ahead of Forward, so Forward's shading
	// work benefits from early-Z (LessOrEqual + DepthWrite=false) instead of
	// re-writing depth it already knows.
	void RenderPreDepthPass(
		GraphicsCommandList& commandList,
		const PreDepthPassResources& resources,
		const PreDepthPassParameters& parameters);
}
