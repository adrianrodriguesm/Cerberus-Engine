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
		if (parameters.DrawCount == 0)
			return;

		DrawArea drawArea;
		drawArea.GraphicsPipeline = resources.Pipeline;
		drawArea.BindGroup0 = resources.BindGroup;
		drawArea.DrawCount = parameters.DrawCount;

		commandList.ExecuteDrawStream(drawArea, parameters.DrawStream);
	}
}
