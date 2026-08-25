#include "pch.h"
#include "Renderer/SceneRenderer/Forward/Passes/SkyPass.h"

#include "Renderer/RHI/Command/GraphicsCommandList.h"

namespace cerberus
{
	void RenderSkyPass(
		GraphicsCommandList& commandList,
		const SkyPassResources& resources)
	{
		commandList.DrawFullscreen(
			resources.Pipeline,
			resources.BindGroup);
	}
}
