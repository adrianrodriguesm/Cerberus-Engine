#include "pch.h"
#include "Matrix.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>
namespace cerberus::math
{
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale)
	{
		using namespace glm;
		using T = float;

		mat4 LocalMatrix(transform);

		// Normalize the matrix.
		if (epsilonEqual(LocalMatrix[3][3], static_cast<float>(0), epsilon<T>()))
			return false;

		// First, isolate perspective.  This is the messiest.
		if (
			epsilonNotEqual(LocalMatrix[0][3], static_cast<T>(0), epsilon<T>()) ||
			epsilonNotEqual(LocalMatrix[1][3], static_cast<T>(0), epsilon<T>()) ||
			epsilonNotEqual(LocalMatrix[2][3], static_cast<T>(0), epsilon<T>()))
		{
			// Clear the perspective partition
			LocalMatrix[0][3] = LocalMatrix[1][3] = LocalMatrix[2][3] = static_cast<T>(0);
			LocalMatrix[3][3] = static_cast<T>(1);
		}

		// Next take care of translation (easy).
		translation = vec3(LocalMatrix[3]);
		LocalMatrix[3] = vec4(0, 0, 0, LocalMatrix[3].w);

		vec3 Row[3];//, Pdum3;

		// Now get scale and shear.
		for (length_t i = 0; i < 3; ++i)
			for (length_t j = 0; j < 3; ++j)
				Row[i][j] = LocalMatrix[i][j];

		// Compute X scale factor and normalize first row.
		scale.x = length(Row[0]);
		Row[0] = detail::scale(Row[0], static_cast<T>(1));
		scale.y = length(Row[1]);
		Row[1] = detail::scale(Row[1], static_cast<T>(1));
		scale.z = length(Row[2]);
		Row[2] = detail::scale(Row[2], static_cast<T>(1));

		rotation.y = asin(-Row[0][2]);
		if (cos(rotation.y) != 0.f) {
			rotation.x = atan2(Row[1][2], Row[2][2]);
			rotation.z = atan2(Row[0][1], Row[0][0]);
		}
		else {
			rotation.x = atan2(-Row[2][0], Row[1][1]);
			rotation.z = 0;
		}


		return true;
	}

	const glm::mat3 MatFactory::CreateZeroMat3()
	{
		return glm::mat3(.0f);
	}

	const glm::mat3 MatFactory::CreateIdentityMat3()
	{
		return glm::mat3(1.f);
	}

	const glm::mat3 MatFactory::CreateScaleMat3(const glm::vec3& scale)
	{
		return glm::mat3(glm::scale(glm::mat4(1.0f), scale));
	}

	const glm::mat3 MatFactory::CreateRotationMat3(const glm::vec3& eulerAngles)
	{
		return glm::toMat3(glm::quat(eulerAngles));
	}

	const glm::mat4 MatFactory::CreateZeroMat4()
	{
		return glm::mat4(.0f);
	}

	const glm::mat4 MatFactory::CreateIdentityMat4()
	{
		return glm::mat4(1.0f);
	}

	const glm::mat4 MatFactory::CreateScaleMat4(const glm::vec3& scale)
	{
		return glm::scale(glm::mat4(1.0f), scale);
	}

	const glm::mat4 MatFactory::CreateRotationMat4(const glm::vec3& eulerAngles)
	{
		return glm::toMat4(glm::quat(eulerAngles));
	}

	const glm::mat4 MatFactory::CreateTranslationMat4(const glm::vec3& position)
	{
		return glm::translate(glm::mat4(1.0f), position);
	}

	const glm::mat4 MatFactory::CreateLookAtMatrix(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up)
	{
		return glm::lookAt(eye, center, up);
	}

	const glm::mat4 MatFactory::CreateOrthographicProjectionMatrix(const float left, const float right, const float bottom, const float top, const float nearPlane, const float farPlane)
	{
		glm::mat4 matrix = glm::ortho(left, right,
									  bottom, top,
									  nearPlane, farPlane);
#if VK_INVERTED_UP_COORD_SYSTEM
		matrix[1][1] *= -1.f;
#endif
		return matrix;
	}

	const glm::mat4 MatFactory::CreatePerspectiveProjectionMatrix(const float fovy, const float width, const float height, const float nearPlane, const float farPlane)
	{
		glm::mat4 matrix = glm::perspectiveFov(fovy, width, height, nearPlane, farPlane);
		// This is due the pipeline being at VK_FRONT_FACE_COUNTER_CLOCKWISE
#if VK_INVERTED_UP_COORD_SYSTEM
		matrix[1][1] *= -1.f;
#endif
		return matrix;
	}
	const glm::mat4 MatFactory::CreateTransformMatrix(const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale)
	{
		return	glm::translate(glm::mat4(1.0f), translation)
			* glm::toMat4(glm::quat(rotation))
			* glm::scale(glm::mat4(1.0f), scale);
	}
}