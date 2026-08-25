#pragma once

#include "Core/Handle.h"

#include <cstdint>
#include <span>

namespace cerberus
{
	class BindGroup;
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
	};

	void RenderForwardPass(
		GraphicsCommandList& commandList,
		const ForwardPassResources& resources,
		const ForwardPassParameters& parameters);
}
