#pragma once
#include "Renderer/Scene/Camera.h"
#include "Core/Timestep.h"
#include "Events/MouseEvent.h"
namespace cerberus
{
	enum class CameraState
	{
		None, FreeMovement, SphericalMovement, PanMovement,
	};

	class EditorCamera : public Camera
	{
	public:
		EditorCamera() = default;
		EditorCamera(float fov, float aspectRatio, float nearClip, float farClip);

		void OnUpdate(Timestep ts);
		void OnEvent(Event& e);

		void SetViewportSize(float width, float height);

		const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
		const glm::mat4& GetViewProjection() const { return m_Projection * m_ViewMatrix; }

		const glm::vec3& GetPosition() const { return m_Position; }
		glm::quat GetOrientation() const;
		glm::vec3 GetUpDirection() const;
		glm::vec3 GetRightDirection() const;
		glm::vec3 GetForwardDirection() const;
		inline CameraState GetCameraState() const { return m_CameraState; }
		void SetFocusPoint(const glm::vec3& focusPoint);
		float GetPitch() const { return m_Pitch; }
		float GetYaw() const { return m_Yaw; }
		float GetNear() const { return m_NearClip; }
		float GetFar() const { return m_FarClip; }
	private:
		void UpdateProjection();
		void UpdateView();
		// Main movements function
		void FreeMovement(Timestep ts, glm::vec2& delta);
		void MousePan(const glm::vec2& delta);
		void SphericalMovement(Timestep ts, glm::vec2& delta);
		glm::vec3 CalculatePosition();
		float ZoomSpeed() const;
		void MouseZoom(float delta);
		// Support movements function 
		glm::vec2 PanSpeed() const;
		glm::vec3 ProcessInput(Timestep ts);
		// Events
		bool OnMouseScroll(MouseScrolledEvent& events);
	private:
		float m_FOV = 65.0f, m_AspectRatio = 1.778f, m_NearClip = 1.f, m_FarClip = 100.0f;
		float m_ViewportWidth = 1280, m_ViewportHeight = 720;

		glm::mat4 m_ViewMatrix;
		float m_Pitch = 0.f, m_Yaw = .0f;

		glm::vec3 m_Position = { 0.0f, 0.0f, 7.0f };
		glm::vec3 m_PositionDelta = {};
		glm::vec3 m_FocalPoint = { 0.0f, 0.0f, 0.0f };

		glm::vec3 m_ViewDirection, m_RightDirection;
		glm::vec2 m_InitialMousePosition;
		// -- Movemenent Pameters
		// Whether or not to invert our Y axis for mouse input to rotation
		bool m_InvertY = false;
		// Speep parementers
		float m_RotationSpeed = .8f;
		float m_Speed = 2.5f, m_SprintMultiplier = 2.f;
		// Exponential boost factor on translation, controllable by mouse wheel
		float m_Boost = 2.f;
		// Time it takes to interpolate camera position 99% of the way to the target
		float m_PositionLerpTime = 0, m_RotationLerpTime = 0.008f;
		float m_Distance = 7.f, m_MinFocusDistance = 100.0f;
		CameraState m_CameraState;
	};
}

