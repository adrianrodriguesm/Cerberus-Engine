#pragma once

#include "Core/Handle.h"

namespace cerberus
{
	class BindGroup;
	class GraphicsCommandList;
	class GraphicsPipeline;

	struct CompositePassResources
	{
		Handle<GraphicsPipeline> Pipeline;
		Handle<BindGroup> BindGroup;
	};

	struct CompositePassParameters
	{
		float Exposure = 1.0f;
	};

	// Fullscreen tonemap (ACES) + exposure + gamma pass: reads the Forward
	// scope's linear-HDR SceneColor and writes the LDR image GetFinalPassImage()
	// exposes. Keeps shading shaders (ForwardPBR/ForwardSky) purely linear so
	// tonemapping isn't duplicated in every shading pass.
	void RenderCompositePass(
		GraphicsCommandList& commandList,
		const CompositePassResources& resources,
		const CompositePassParameters& parameters);
}
