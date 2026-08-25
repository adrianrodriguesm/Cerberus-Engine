#include "pch.h"
#include "Renderer/SceneRenderer/Forward/Passes/CompositePass.h"

#include "Renderer/RHI/Command/GraphicsCommandList.h"

namespace cerberus
{
	namespace
	{
		struct CompositePushConstants
		{
			float Exposure = 1.0f;
		};
	}

	void RenderCompositePass(
		GraphicsCommandList& commandList,
		const CompositePassResources& resources,
		const CompositePassParameters& parameters)
	{
		commandList.BindGraphicsPipeline(resources.Pipeline);
		if (resources.BindGroup.IsValid())
			commandList.BindDescriptorSets(resources.BindGroup, 0);

		CompositePushConstants pushConstants;
		pushConstants.Exposure = parameters.Exposure;
		commandList.PushConstants(VK_SHADER_STAGE_FRAGMENT_BIT, AsBytes(pushConstants));

		commandList.Draw(3);
	}
}
