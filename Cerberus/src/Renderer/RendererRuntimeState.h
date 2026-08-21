#pragma once

#include "Core/Base.h"
#include "Renderer/RendererContext.h"
#include "Renderer/CommandBuffer.h"
#include "Renderer/CommandList.h"
#include "Renderer/CommandPool.h"
#include "Renderer/TransientCommandExecutor.h"

namespace cerberus
{
	// Global renderer state grouped by ownership/responsibility.
	// This keeps Renderer as the public facade while making it explicit which
	// subsystem owns frame state, command resources, descriptors and UI.
	struct RendererCommandState
	{
		Scope<CommandPool> GraphicsCommandPool;
		Scope<TransientCommandExecutor> TransientExecutor;

		Scope<CommandBuffer> MainCommandBuffer;
		Scope<CommandBuffer> SwapchainCommandBuffer;
	};

	struct RendererFrameState
	{
		VkPhysicalDeviceLimits DeviceLimits{};
		RendererFrameStatistics Statistics{};
	};
}
