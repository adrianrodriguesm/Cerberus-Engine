#pragma once
#include "Renderer/RHI/Texture.h"

#include <optional>
#include <vector>

namespace cerberus
{
	enum class LOAD_OP : uint8_t
	{
		CLEAR,
		DONT_CARE,
		LOAD,
		NONE
	};
	enum class STORE_OP : uint8_t
	{
		DONT_CARE,
		STORE,
		NONE
	};

	// Cerberus-side description of one Dynamic Rendering attachment. SceneRenderer
	// code builds these instead of touching VkRenderingAttachmentInfo directly;
	// GraphicsCommandList::BeginRendering translates them and owns the required
	// layout transitions.
	struct RenderingAttachmentInfo
	{
		Handle<RenderTargetTexture> Target;
		LOAD_OP LoadOp = LOAD_OP::CLEAR;
		STORE_OP StoreOp = STORE_OP::STORE;
	};

	// Describes one Dynamic Rendering scope (a vkCmdBeginRendering/vkCmdEndRendering
	// pair). A logical pass such as SkyPass or ForwardPass is not required to open
	// its own scope; several logical passes may record into the same RenderingInfo.
	struct RenderingInfo
	{
		std::vector<RenderingAttachmentInfo> ColorAttachments;
		std::optional<RenderingAttachmentInfo> DepthAttachment;

		uint32_t Width = 0;
		uint32_t Height = 0;
	};
}
