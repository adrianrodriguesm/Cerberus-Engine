#pragma once

#include "Core/Handle.h"
#include "Renderer/Scene/Proxies/MaterialProxy.h"
#include "Renderer/Scene/Proxies/MeshProxy.h"

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
		std::span<const Handle<MeshInstanceProxy>> MeshInstances;
		std::span<const MaterialProxy> MaterialProxies;
	};

	void RenderForwardPass(
		GraphicsCommandList& commandList,
		const ForwardPassResources& resources,
		const ForwardPassParameters& parameters);
}
