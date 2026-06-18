#pragma once

#include "Renderer/CommandBuffer.h"

namespace cerberus
{
	// Owns Dear ImGui renderer resources and frame rendering.
	// Renderer only coordinates this subsystem; ImGui descriptor pool,
	// secondary command buffers and backend init/shutdown live here.
	class ImGuiRenderer
	{
	public:
		static void Init();
		static void Shutdown();

		static void Render();

		static void BeginFrame();
		static void EndFrame();
		static void SetColorTheme();
	};
}
