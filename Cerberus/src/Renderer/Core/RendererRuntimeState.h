#pragma once

#include "Core/Base.h"
#include "Renderer/Core/RendererContext.h"
#include "Renderer/RHI/Command/CommandBuffer.h"
#include "Renderer/RHI/Command/CommandList.h"
#include "Renderer/RHI/Command/CommandPool.h"
#include "Renderer/RHI/Command/TransientCommandExecutor.h"

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
