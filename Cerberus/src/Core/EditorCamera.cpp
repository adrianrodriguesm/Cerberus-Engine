#include "pch.h"
#include "EditorCamera.h"
#include "Core/Input.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace cerberus
{
	EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
		: m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip),
		m_FocalPoint(m_Position), m_CameraState(CameraState::None)
	{

		UpdateView();
		UpdateProjection();
	}
	////////////////////////////////////////////////////////////////////
	///// Updates
	////////////////////////////////////////////////////////////////////
	void EditorCamera::OnUpdate(Timestep ts)
	{
		glm::vec2 mouse{ Input::GetMouseX(), Input::GetMouseY() };
		//math::Vec2 delta = math::Vec2(mouse.x - m_InitialMousePosition.x, m_InitialMousePosition.y - mouse.y);
		glm::vec2 delta = (mouse - m_InitialMousePosition) * 0.002f;
		// FreeMovement
		if (Input::IsMouseButtonPressed(Mouse::ButtonRight))
		{
			m_CameraState = CameraState::FreeMovement;
			Input::DisableCursor();
			FreeMovement(ts, delta);
		}
		else if (Input::IsKeyPressed(Key::LeftAlt) && Input::IsMouseButtonPressed(Mouse::ButtonLeft))
		{
			m_CameraState = CameraState::SphericalMovement;
			Input::DisableCursor();
			SphericalMovement(ts, delta);
		}
		// PanMovement
		else if (Input::IsMouseButtonPressed(Mouse::ButtonMiddle))
		{
			m_CameraState = CameraState::PanMovement;
			Input::DisableCursor();
			MousePan(delta);
		}
		else
		{
			m_CameraState = CameraState::None;
			Input::EnableCursor();
		}

		if (m_CameraState == CameraState::FreeMovement)
			m_Position += m_PositionDelta;
		else
			m_Position = CalculatePosition();

		m_InitialMousePosition = mouse;

		UpdateView();
	}

	glm::vec3 EditorCamera::CalculatePosition()
	{
		return m_FocalPoint - GetForwardDirection() * m_Distance + m_PositionDelta;
	}
	void EditorCamera::UpdateProjection()
	{
		m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
		m_Projection = glm::perspective(glm::radians(m_FOV), m_AspectRatio, m_NearClip, m_FarClip);
		// This is due the pipeline being at VK_FRONT_FACE_COUNTER_CLOCKWISE
		m_Projection[1][1] *= -1.f;
	}
	void EditorCamera::UpdateView()
	{
		const float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;
		// Extra step to handle the problem when the camera direction is the same as the up vector
		const float cosAngle = glm::dot(GetForwardDirection(), GetUpDirection());
		if (cosAngle * yawSign > 0.99f)
			m_Pitch = 0.f;

		const glm::vec3 lookAt = m_Position + GetForwardDirection();
		m_ViewDirection = glm::normalize(m_FocalPoint - m_Position);
		m_FocalPoint = m_Position + GetForwardDirection() * m_Distance;
		m_ViewMatrix = glm::lookAt(m_Position, lookAt, { 0, yawSign , 0 });

		m_PositionDelta *= 0.8f;
	}
	////////////////////////////////////////////////////////////////////
	///// Free Movements
	////////////////////////////////////////////////////////////////////
	glm::vec3 EditorCamera::ProcessInput(Timestep ts)
	{
		glm::vec3 movement = { 0,0,0 };
		//math::Vec3 view = m_FocalPoint - m_Position;
		const float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;

		// Forward/Back
		if (Input::IsKeyPressed(Key::W))
			movement += m_ViewDirection;
		else if (Input::IsKeyPressed(Key::S))
			movement -= m_ViewDirection;

		// Left/Rigth
		if (Input::IsKeyPressed(Key::D))
			movement += m_RightDirection;
		else if (Input::IsKeyPressed(Key::A))
			movement -= m_RightDirection;

		// Up/Down
		if (Input::IsKeyPressed(Key::E))
			movement += glm::vec3(0.f, 1.f, 0.f);
		else if (Input::IsKeyPressed(Key::Q))
			movement += glm::vec3(0.f, -1.f, 0.f);

		m_RightDirection = glm::cross(m_ViewDirection, glm::vec3{ 0.f, yawSign, 0.f });

		movement = movement != glm::vec3(0.f) ? glm::normalize(movement) : movement;
		float speed = Input::IsKeyPressed(Key::LeftShift) ? m_Speed * m_SprintMultiplier : m_Speed;
		speed *= powf(2.f, m_Boost);
		return movement * speed * ts.GetSeconds();
	}
	void EditorCamera::FreeMovement(Timestep ts, glm::vec2& delta)
	{
		m_PositionDelta += ProcessInput(ts);
		/** /
		math::Vec3 newPosition = m_Position + ProcessInput(ts);
		// Framerate-independent interpolation
		// Calculate the lerp amount, such that we get 99% of the way to our target in the specified time
		float positionLerpPct = 1.f - expf((logf(1.f - 0.99f) / m_PositionLerpTime) * ts);
		m_Position = math::Lerp(m_Position, newPosition, positionLerpPct);
		/**/

		constexpr float maxRate{ 0.12f };
		const float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;

		float newYaw = m_Yaw + std::clamp(yawSign * delta.x, -maxRate, maxRate);
		float newPitch = m_Pitch + std::clamp(delta.y, -maxRate, maxRate);
		// Framerate-independent interpolation
		// Calculate the lerp amount, such that we get 99% of the way to our target in the specified time	
		float rotationLerpPct = 1.f - expf((logf(1.f - 0.99f) / m_RotationLerpTime) * ts);
		m_Yaw = glm::mix(m_Yaw, newYaw, rotationLerpPct);
		m_Pitch = glm::mix(m_Pitch, newPitch, rotationLerpPct);

	}
	void EditorCamera::SetFocusPoint(const glm::vec3& focusPoint)
	{
		m_FocalPoint = focusPoint;
		if (m_Distance > m_MinFocusDistance)
		{
			const float distance = m_Distance - m_MinFocusDistance;
			MouseZoom(distance / ZoomSpeed());
			m_CameraState = CameraState::SphericalMovement;
		}
		m_Position = m_FocalPoint - GetForwardDirection() * m_Distance + m_PositionDelta;
		UpdateView();
	}

	////////////////////////////////////////////////////////////////////
	///// Pan Movements
	////////////////////////////////////////////////////////////////////
	void EditorCamera::MousePan(const glm::vec2& delta)
	{
		glm::vec2 speed = PanSpeed();
		m_FocalPoint += -GetRightDirection() * speed.x * delta.x * m_Distance;
		m_FocalPoint += GetUpDirection() * speed.y * delta.y * m_Distance;
	}
	glm::vec2 EditorCamera::PanSpeed() const
	{
		float x = std::min(m_ViewportWidth / 1000.0f, 2.4f); // max = 2.4f
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		float y = std::min(m_ViewportHeight / 1000.0f, 2.4f); // max = 2.4f
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return { xFactor, yFactor };
	}
	////////////////////////////////////////////////////////////////////
	///// Spherical Movements
	////////////////////////////////////////////////////////////////////
	void EditorCamera::SphericalMovement(Timestep ts, glm::vec2& delta)
	{
		const float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;
		/** /
		m_YawDelta += yawSign * delta.x * m_RotationSpeed;
		m_PitchDelta += delta.y * m_RotationSpeed;

		/**/
		float newYaw = m_Yaw + yawSign * delta.x * m_RotationSpeed;
		float newPitch = m_Pitch + delta.y * m_RotationSpeed;
		// Framerate-independent interpolation
		// Calculate the lerp amount, such that we get 99% of the way to our target in the specified time	
		float rotationLerpPct = 1.f - expf((logf(1.f - 0.99f) / m_RotationLerpTime) * ts);
		m_Yaw = glm::mix(m_Yaw, newYaw, rotationLerpPct);
		m_Pitch = glm::mix(m_Pitch, newPitch, rotationLerpPct);
		/**/
	}

	glm::quat EditorCamera::GetOrientation() const
	{
		return glm::quat({ -m_Pitch, -m_Yaw, 0.0f });
	}
	glm::vec3 EditorCamera::GetUpDirection() const
	{
		glm::quat orientation = GetOrientation();
		return glm::rotate(orientation, { 0.0f, 1.0f, 0.0f });
	}
	glm::vec3 EditorCamera::GetRightDirection() const
	{
		return glm::rotate(GetOrientation(), { 1.0f, 0.0f, 0.0f });
	}
	glm::vec3 EditorCamera::GetForwardDirection() const
	{
		return glm::rotate(GetOrientation(), { 0.0f, 0.0f, -1.0f });
	}

	float EditorCamera::ZoomSpeed() const
	{
		float distance = m_Distance * 0.2f;
		distance = std::max(distance, 0.0f);
		float speed = distance * distance;
		speed = std::min(speed, 100.0f); // max speed = 100
		return speed;
	}

	void EditorCamera::MouseZoom(float delta)
	{
		m_Distance -= delta * ZoomSpeed();
		m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
		const glm::vec3 forwardDir = GetForwardDirection();
		if (m_Distance < 1.0f)
		{
			m_FocalPoint += forwardDir;
			m_Distance = 1.0f;
		}
		m_PositionDelta += delta * ZoomSpeed() * forwardDir;
	}
	////////////////////////////////////////////////////////////////////
	///// Events
	////////////////////////////////////////////////////////////////////
	void EditorCamera::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(BIND_EVENT_FN(EditorCamera::OnMouseScroll));

	}
	void EditorCamera::SetViewportSize(float width, float height)
	{
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		UpdateProjection();
	}

	bool EditorCamera::OnMouseScroll(MouseScrolledEvent& event)
	{
		if (m_CameraState == CameraState::FreeMovement)
			m_Boost = std::clamp(m_Boost + event.GetYOffset() * 0.07f, 0.f, 3.5f);
		else
			MouseZoom(event.GetYOffset() * 0.1f);

		return false;
	}
}