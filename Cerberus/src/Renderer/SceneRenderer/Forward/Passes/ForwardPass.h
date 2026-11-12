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

	struct ForwardPassResources
	{
		Handle<GraphicsPipeline> Pipeline;
		Handle<BindGroup> BindGroup;
	};

	struct ForwardPassParameters
	{
		std::span<const uint32_t> DrawStream;
		uint32_t DrawCount = 0;
		// GeometryPool vertex buffer address, for vertex pulling -- see
		// docs/tasks/T3-geometry-pool.md.
		uint64_t VertexBufferAddress = 0;
		// GPUInstanceData buffer address, read via Instances[gl_InstanceIndex]
		// -- see docs/tasks/T4-instances-indirect.md.
		uint64_t InstanceBufferAddress = 0;
		Handle<GPUBuffer> IndexBuffer;

		bool UseIndirect = false;
		Handle<GPUBuffer> IndirectArgs;
		uint32_t IndirectDrawCount = 0;
	};

	void RenderForwardPass(
		GraphicsCommandList& commandList,
		const ForwardPassResources& resources,
		const ForwardPassParameters& parameters);
}
