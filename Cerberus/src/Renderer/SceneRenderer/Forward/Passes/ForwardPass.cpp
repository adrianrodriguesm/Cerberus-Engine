#include "pch.h"
#include "Renderer/SceneRenderer/Forward/Passes/ForwardPass.h"

#include "Renderer/RHI/Command/GraphicsCommandList.h"

namespace cerberus
{
	void RenderForwardPass(
		GraphicsCommandList& commandList,
		const ForwardPassResources& resources,
		const ForwardPassParameters& parameters)
	{
		DrawArea drawArea;
		drawArea.GraphicsPipeline = resources.Pipeline;
		drawArea.BindGroup0 = resources.BindGroup;

		commandList.RenderMeshInstances(
			drawArea,
			parameters.MeshInstances,
			parameters.MaterialProxies);
	}
}
