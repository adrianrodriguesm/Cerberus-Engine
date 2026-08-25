#pragma once

#include "Core/Handle.h"

namespace cerberus
{
	class BindGroup;
	class GraphicsCommandList;
	class GraphicsPipeline;

	struct SkyPassResources
	{
		Handle<GraphicsPipeline> Pipeline;
		Handle<BindGroup> BindGroup;
	};

	void RenderSkyPass(
		GraphicsCommandList& commandList,
		const SkyPassResources& resources);
}
