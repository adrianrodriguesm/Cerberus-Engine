#include "pch.h"
#include "Input.h"
#include "Core/Application.h"
namespace cerberus
{
	bool Input::IsKeyPressed(const KeyCode keyCode)
	{
		auto win = Application::Get().GetWindow()->GetNativeWindow();
		int state = glfwGetKey(win, keyCode);
		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}
	bool Input::IsMouseButtonPressed(const MouseCode button)
	{
		auto win = Application::Get().GetWindow()->GetNativeWindow();
		int state = glfwGetMouseButton(win, button);
		return state == GLFW_PRESS;
	}
	glm::vec2 Input::GetMousePosition()
	{
		auto win = Application::Get().GetWindow()->GetNativeWindow();
		double xpos, ypos;
		glfwGetCursorPos(win, &xpos, &ypos);
		return glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
	}
	float Input::GetMouseX()
	{
		return GetMousePosition().x;
	}
	float Input::GetMouseY()
	{
		return GetMousePosition().y;
	}
	void Input::EnableCursor()
	{
		Application::Get().GetWindow()->SetCursor(true);
	}
	void Input::DisableCursor()
	{
		Application::Get().GetWindow()->SetCursor(false);
	}
}
