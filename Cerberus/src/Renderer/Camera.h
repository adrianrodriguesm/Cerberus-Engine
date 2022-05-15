#pragma once

namespace cerberus
{
	class Camera
	{
	public:
		Camera() = default;
		Camera(const glm::mat4& projection)
			: m_Projection(projection), m_Exposure(1.f) {}
		virtual ~Camera() = default;

		const glm::mat4 GetProjection() const { return m_Projection; }
		const glm::mat4 GetReverseProjection() const { return m_ReverseProjection; }
		void SetProjection(const glm::mat4& projection) { m_Projection = projection; }

		const float GetExposure() const { return m_Exposure; }
		void SetExposure(const float exposure) { m_Exposure = exposure; }

	protected:
		glm::mat4 m_Projection, m_ReverseProjection;
		float m_Exposure = .8f;
	};
}