#include "pch.h"
#include "FlyingCamera.h"
#include "Core/Input.h"
#include "Core/Application.h"
#include <glm/glm/ext/matrix_clip_space.hpp>
#include <glm/glm/ext/matrix_transform.hpp>
#include "Math/Matrix.h"
namespace cerberus
{
	FlyingCamera::FlyingCamera(float fov, float aspectRatio, float nearClip, float farClip)
		: m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip), 
		m_Target(m_Position + GetDirection()), m_State(CameraState::None), m_Distance(glm::distance(m_Position, m_Target))
	{
		UpdateView();
		UpdateProjection();
	}
	////////////////////////////////////////////////////////////////////
	///// Updates
	////////////////////////////////////////////////////////////////////
	void FlyingCamera::OnUpdate(Timestep ts)
	{
		glm::vec2 mouse{ Input::GetMouseX(), Input::GetMouseY() };
		glm::vec2 delta = glm::vec2(mouse.x - m_InitialMousePosition.x, m_InitialMousePosition.y - mouse.y);

		// FreeMovement
		if (Input::IsMouseButtonPressed(Mouse::ButtonRight))
		{
			m_State = CameraState::FreeMovement;
			Input::DisableCursor();
			FreeMovement(ts, delta);
		}
		// PanMovement
		else if (Input::IsMouseButtonPressed(Mouse::ButtonMiddle))
		{
			m_State = CameraState::PanMovement;
			Input::DisableCursor();
			MousePan(delta);
		}
		else
		{
			m_State = CameraState::None;
			Input::EnableCursor();
		}


		m_InitialMousePosition = mouse;
		UpdateView();
	}

	void FlyingCamera::UpdateProjection()
	{
		if (m_ViewportHeight <= 0)
			return;

		m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
		m_Projection = math::MatFactory::CreatePerspectiveProjectionMatrix(glm::radians(m_FOV), m_ViewportWidth, m_ViewportHeight, m_NearClip, m_FarClip);
		m_ReverseProjection = math::MatFactory::CreatePerspectiveProjectionMatrix(glm::radians(m_FOV), m_ViewportWidth, m_ViewportHeight, m_FarClip, m_NearClip);
	}
	void FlyingCamera::UpdateView()
	{
		m_ViewMatrix = math::MatFactory::CreateLookAtMatrix(m_Position, m_Target, m_Up);
	}
	////////////////////////////////////////////////////////////////////
	///// Free Movements
	////////////////////////////////////////////////////////////////////
	glm::vec3 FlyingCamera::ProcessInput(Timestep ts)
	{
		glm::vec3 movement = { 0,0,0 };
		glm::vec3 view = m_Target - m_Position;
		// Forward/Back
		if (Input::IsKeyPressed(Key::W))
			movement += view;
		else if (Input::IsKeyPressed(Key::S))
			movement -= view;

		// Left/Rigth
		if (Input::IsKeyPressed(Key::D))
			movement += glm::cross(view, m_Up);
		else if (Input::IsKeyPressed(Key::A))
			movement -= glm::cross(view, m_Up);

		// Up/Down
		if (Input::IsKeyPressed(Key::E))
			movement += glm::vec3(0,1.f,0);
		else if (Input::IsKeyPressed(Key::Q))
			movement += glm::vec3(0, -1.f, 0);

		float speed = Input::IsKeyPressed(Key::LeftShift) ? m_Speed * m_SprintMultiplier : m_Speed;
		speed *= powf(2.f, m_Boost);
		movement = movement != glm::vec3(0, 0, 0.f) ? glm::normalize(movement) : movement;
		return movement * speed * (float)ts;
	}

	void FlyingCamera::ProcessMouseMovement(Timestep ts, glm::vec2 delta)
	{
		// Framerate-independent interpolation
		// Calculate the lerp amount, such that we get 99% of the way to our target in the specified time	
		float rotationLerpPct = 1.f - expf((logf(1.f - 0.99f) / m_RotationLerpTime) * ts);
		m_Yaw = glm::mix(m_Yaw, m_Yaw + delta.x * m_Sensibility, rotationLerpPct);
		m_Pitch = glm::mix(m_Pitch, m_Pitch + delta.y * m_Sensibility, rotationLerpPct);
		m_Pitch = std::clamp(m_Pitch, -89.f, 89.f);
	}
	void FlyingCamera::FreeMovement(Timestep ts, glm::vec2 delta)
	{
		// Mouse	
		ProcessMouseMovement(ts, delta);
		// Input Key
		glm::vec3 movement = ProcessInput(ts);
		glm::vec3 direction = GetDirection();
		/** /
		// Framerate-independent interpolation
		// Calculate the lerp amount, such that we get 99% of the way to our target in the specified time	
		float positionLerpPct = 1.f - expf((logf(1.f - 0.99f) / m_PositionLerpTime) * ts);
		m_Position = glm::mix(m_Position, m_Position + movement, positionLerpPct);
		/**/
		m_Position += movement;
		m_Target = m_Position + direction;

	}
	void FlyingCamera::SetFocusPoint(const glm::vec3& focusPoint, float focusRadius)
	{
		const float halfVerticalFOV = glm::radians(m_FOV) * 0.5f;
		const float halfHorizontalFOV = atanf(tanf(halfVerticalFOV) * m_AspectRatio);
		const float limitingHalfFOV = glm::max(glm::min(halfVerticalFOV, halfHorizontalFOV), glm::radians(1.0f));

		focusRadius = glm::max(focusRadius, 0.1f);
		m_Distance = glm::max((focusRadius / tanf(limitingHalfFOV)) * 1.25f, m_NearClip * 2.0f);
		m_NewTarget = focusPoint;
		m_Target = focusPoint;
		m_Position = focusPoint - GetDirection() * m_Distance;
		UpdateView();
	}

	void FlyingCamera::SetPose(const glm::vec3& position, float yaw, float pitch)
	{
		m_Position = position;
		m_Yaw = yaw;
		m_Pitch = std::clamp(pitch, -89.f, 89.f);
		m_Target = m_Position + GetDirection();
		m_NewTarget = m_Target;
		m_TargetPosition = m_Position;
		m_TargetYaw = m_Yaw;
		m_TargetPitch = m_Pitch;
		m_Distance = glm::distance(m_Position, m_Target);
		UpdateView();
	}

	glm::vec3 FlyingCamera::GetDirection() const
	{
		float yawRad = glm::radians(m_Yaw);
		float pitchRad = glm::radians(m_Pitch);

		glm::vec3 direction;
		direction.x = cosf(yawRad) * cosf(pitchRad);
		direction.y = ((m_InvertY) ? -1.f : 1.f) * sinf(pitchRad);
		direction.z = sinf(yawRad) * cosf(pitchRad);

		return glm::normalize(direction);
	}
	////////////////////////////////////////////////////////////////////
	///// Pan Movements
	////////////////////////////////////////////////////////////////////
	void FlyingCamera::MousePan(const glm::vec2& delta)
	{
		glm::vec2 speed = PanSpeed();
		auto xSpeed = speed.x;
		auto ySpeed = speed.y;
		glm::vec3 view = glm::normalize(m_Target - m_Position);
		glm::vec3 sideMovement = glm::normalize(glm::cross(view, m_Up));
		glm::vec3 verticalMovement = ((m_InvertY) ? 1.f : -1.f) * glm::normalize(glm::cross(view, sideMovement));
		glm::vec3 movement = verticalMovement * delta.y * ySpeed + sideMovement * delta.x * xSpeed;
		m_Target += movement;
		m_Position += movement;
	}
	glm::vec2 FlyingCamera::PanSpeed() const
	{
		float x = std::min(m_ViewportWidth / 1000.0f, 2.4f); // max = 2.4f
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		float y = std::min(m_ViewportHeight / 1000.0f, 2.4f); // max = 2.4f
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return { xFactor, yFactor };
	}
	glm::vec3 FlyingCamera::GetRightAxis()
	{
		return glm::normalize(glm::cross((m_Target - m_Position), m_Up));
	}
	glm::vec3 FlyingCamera::GetViewDir()
	{
		return glm::normalize( m_Target - m_Position);
	}
	////////////////////////////////////////////////////////////////////
	///// Events
	////////////////////////////////////////////////////////////////////
	void FlyingCamera::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(BIND_EVENT_FN(FlyingCamera::OnMouseScroll));
	}
	void FlyingCamera::SetViewportSize(float width, float height)
	{
		if (m_ViewportWidth != width || m_ViewportHeight != height)
		{
			m_ViewportWidth = width;
			m_ViewportHeight = height;
			UpdateProjection();
		}

	}

	bool FlyingCamera::OnMouseScroll(MouseScrolledEvent& event)
	{
		if (m_State != CameraState::FreeMovement)
		{
			Timestep ts = Application::Get().GetTimestep();
			glm::vec3 movement = {};
			if (event.GetYOffset() > 0)
				movement += GetViewDir() * (float)ts;
			else
				movement -= GetViewDir() * (float)ts;

			movement *= Input::IsKeyPressed(Key::LeftShift) ? m_SprintMultiplier * m_ScrollSpeed : m_ScrollSpeed;
			// Calculate the lerp amount, such that we get 99% of the way to our target in the specified time	
			float positionLerpPct = 1.f - expf((logf(1.f - 0.99f) / m_PositionLerpTime) * ts);
			m_Position = glm::mix(m_Position, m_Position + movement, positionLerpPct);
			m_Target = glm::mix(m_Target, m_Target + movement, positionLerpPct);
		}
		else
		{
			// Modify movement by a boost factor
			m_Boost += event.GetYOffset() * 0.2f;
		}
		return false;
	}
}
