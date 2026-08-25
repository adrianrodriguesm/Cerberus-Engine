#pragma once

#include "Core/Handle.h"

#include <cstdint>
#include <span>

namespace cerberus
{
	class BindGroup;
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
	};

	// Depth-only pass writing SceneDepth ahead of Forward, so Forward's shading
	// work benefits from early-Z (LessOrEqual + DepthWrite=false) instead of
	// re-writing depth it already knows.
	void RenderPreDepthPass(
		GraphicsCommandList& commandList,
		const PreDepthPassResources& resources,
		const PreDepthPassParameters& parameters);
}
