#pragma once

namespace cerberus::math
{
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale);

	class MatFactory
	{
	public:
		static const glm::mat3 CreateZeroMat3();
		static const glm::mat3 CreateIdentityMat3();

		static const glm::mat3 CreateScaleMat3(const glm::vec3& scale);
		static const glm::mat3 CreateRotationMat3(const glm::vec3& eulerAngles);

		static const glm::mat4 CreateZeroMat4();
		static const glm::mat4 CreateIdentityMat4();

		static const glm::mat4 CreateScaleMat4(const glm::vec3& scale);
		static const glm::mat4 CreateRotationMat4(const glm::vec3& eulerAngles);
		static const glm::mat4 CreateTranslationMat4(const glm::vec3& position);
		static const glm::mat4 CreateTransformMatrix(const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale);

		static const glm::mat4 CreateLookAtMatrix(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up);
		static const glm::mat4 CreateOrthographicProjectionMatrix(const float left, const float right, const float bottom, const float top, const float nearPlane, const float farPlane);
		static const glm::mat4 CreatePerspectiveProjectionMatrix(const float fovy, const float width, const float height, const float nearPlane, const float farPlane);
	};
}