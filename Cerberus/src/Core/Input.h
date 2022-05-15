#pragma once
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"

namespace cerberus
{
	class Input
	{
	public:
		static bool IsKeyPressed(const KeyCode keyCode);
		static bool IsMouseButtonPressed(const MouseCode button);
		static glm::vec2 GetMousePosition();
		static float GetMouseX();
		static float GetMouseY();
		static void EnableCursor();
		static void DisableCursor();

	};

}