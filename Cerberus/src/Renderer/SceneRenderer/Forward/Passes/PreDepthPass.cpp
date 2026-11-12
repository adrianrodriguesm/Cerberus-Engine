#include "pch.h"
#include "Renderer/SceneRenderer/Forward/Passes/PreDepthPass.h"

#include "Renderer/RHI/Command/GraphicsCommandList.h"

namespace cerberus
{
	void RenderPreDepthPass(
		GraphicsCommandList& commandList,
		const PreDepthPassResources& resources,
		const PreDepthPassParameters& parameters)
	{
		if ((parameters.UseIndirect ? parameters.IndirectDrawCount : parameters.DrawCount) == 0)
			return;

		DrawArea drawArea;
		drawArea.GraphicsPipeline = resources.Pipeline;
		drawArea.BindGroup0 = resources.BindGroup;
		drawArea.DrawCount = parameters.DrawCount;
		drawArea.VertexBufferAddress = parameters.VertexBufferAddress;
		drawArea.InstanceBufferAddress = parameters.InstanceBufferAddress;
		drawArea.IndexBuffer = parameters.IndexBuffer;

		if (parameters.UseIndirect)
			commandList.ExecuteDrawIndirect(drawArea, parameters.IndirectArgs, parameters.IndirectDrawCount);
		else
			commandList.ExecuteDrawStream(drawArea, parameters.DrawStream);
	}
}
