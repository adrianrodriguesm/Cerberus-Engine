#pragma once
#include "Renderer/Scene/Camera.h"
#include "Events/MouseEvent.h"
#include "Core/Timestep.h"
namespace cerberus
{
	enum class CameraState
	{
		None, FreeMovement, SphericalMovement, PanMovement,
	};

	class FlyingCamera : public Camera
	{
	public:
		FlyingCamera() = default;
		FlyingCamera(float fov, float aspectRatio, float nearClip, float farClip);

		void OnUpdate(Timestep ts);
		void OnEvent(Event& e);

		void SetViewportSize(float width, float height);

		const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
		const glm::mat4& GetViewProjectionMatrix() const { return m_Projection * m_ViewMatrix; }
	

		const glm::vec3& GetPosition() const { return m_Position; }
		glm::vec3 GetDirection() const;
		void SetFocusPoint(const glm::vec3& focusPoint, float focusRadius = 1.0f);
		float GetPitch() const { return m_Pitch; }
		float GetYaw() const { return m_Yaw; }
		float GetNearPlane() const { return m_NearClip; }
		float GetFarPlane() const { return m_FarClip; }
		const glm::vec3& GetTarget() const { return m_Target; }
	private:
		void UpdateProjection();
		void UpdateView();
		// Main movements function
		void FreeMovement(Timestep ts, glm::vec2 delta);
		void MousePan(const glm::vec2& delta);
		glm::vec3 GetRightAxis();
		glm::vec3 GetViewDir();
		// Support movements function 
		glm::vec2 PanSpeed() const;
		glm::vec3 ProcessInput(Timestep ts);
		void ProcessMouseMovement(Timestep ts, glm::vec2 delta);
		// Events
		bool OnMouseScroll(MouseScrolledEvent& events);
	private:
		float m_FOV = 55.0f, m_AspectRatio = 1.778f, m_NearClip = 0.1f, m_FarClip = 10000.0f;
		float m_ViewportWidth = 1280, m_ViewportHeight = 720;

		glm::mat4 m_ViewMatrix;

		float m_Pitch = 0.f, m_Yaw = -90.0f;

		glm::vec3 m_Position = { 0.0f, 0.0f, 7.0f };
		glm::vec3 m_Target = { 0.0f, 0.0f, 0.0f };
		glm::vec3 m_Up = { 0.f, 1.f, 0.f };
		glm::vec2 m_InitialMousePosition = { 0.0f, 0.0f };
		// -- Movemenent Pameters
		// Whether or not to invert our Y axis for mouse input to rotation
		bool m_InvertY = false;
		// Mouse Sensibility
		float m_Sensibility = 0.025f;
		// Speep parementers
		float m_Speed = 6.f, m_SprintMultiplier = 2.5f;
		float m_ScrollSpeed = 8.f, m_ScrollSprintMultiplier = 2.5f;
		// Exponential boost factor on translation, controllable by mouse wheel
		float m_Boost = 2.f;
		// Time it takes to interpolate camera position 99% of the way to the target
		float m_PositionLerpTime = 0.02f, m_RotationLerpTime = 0.008f;
		float m_Distance;
		glm::vec3 m_DeltaAngle;
		CameraState m_State;
		// Plane Movement
		//float m_Distance = 100.f;
		glm::vec3 m_TargetPosition = {0,0,0}, m_NewTarget;
		float m_TargetYaw = 0, m_TargetPitch = 0;
	};
}


