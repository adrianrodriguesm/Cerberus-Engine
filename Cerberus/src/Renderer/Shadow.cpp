#include "pch.h"
#include "Shadow.h"
#include "Renderer/Renderer.h"
#include <glm/glm/ext/matrix_transform.hpp>
#include <glm/glm/ext/matrix_clip_space.hpp>
#include <glm/gtx/string_cast.hpp>
#include "Math/Matrix.h"
namespace cerberus
{
	Shadow::~Shadow()
	{
	}
	void Shadow::UpdateShadowCascade(const SceneRendererCamera& camera, const glm::mat4& inverseViewProjection, const glm::vec3& lightDirection)
	{
		float minZ = camera.Near;
		float maxZ = camera.Far;
		float rangeZ = maxZ - minZ;
		float ratio = maxZ / minZ;
		const float shadowMapResolution = (float)Renderer::GetRendererConfig().ShadowMapResolution;

		uint32_t shadowCascadesCount = Renderer::GetRendererConfig().ShadowCascadesCount;
		float cascadeSplits[4];

		// Calculate split depths based on view camera frustum
		// Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
		// Log and unifor schemes can't produce appropriate sampling densities for the near and far depth 
		// layers simultaneously, the practical split scheme integrates logarithmic and uniform schemes 
		// to produce moderate sampling densities over the whole depth range. 
		for (uint32_t i = 0; i < shadowCascadesCount; i++)
		{
			// Portion of the map
			float portion = (i + 1) / (float)shadowCascadesCount;
			// Logarithmic split scheme produce optimal distribution   
			// of perspective aliasing over the whole depth range
			float log = minZ * std::pow(ratio, portion);
			// Uniform split scheme is the same as in standard shadow maps
			// simply places the split planes evenly along the view direction
			float uniform = minZ + rangeZ * portion;
			// Interpolate between the two splits schemes
			float splitDistance = m_CascadeSplitLambda * log + (1 - m_CascadeSplitLambda) * uniform;
			// Cascade split in a range between 0 to 1
			cascadeSplits[i] = (splitDistance - minZ) / rangeZ;
		}

		
		// Calculate orthographic projection matrix for each cascade
		glm::mat4 invViewProjection = inverseViewProjection;
		

		glm::vec3 frustumCorners[8] =
		{
			// Front (negative Z)
			glm::vec3(-1.0f,  1.0f, -1.0f),	// Top-Left
			glm::vec3(1.0f,  1.0f, -1.0f),	// Top-Right
			glm::vec3(1.0f, -1.0f, -1.0f),	// Bottom-Right
			glm::vec3(-1.0f, -1.0f, -1.0f),	// Bottom-Left
			// Back (positive Z)
			glm::vec3(-1.0f,  1.0f,  1.0f), // Top-Left
			glm::vec3(1.0f,  1.0f,  1.0f),	// Top-Right
			glm::vec3(1.0f, -1.0f,  1.0f),	// Bottom-Right
			glm::vec3(-1.0f, -1.0f,  1.0f),	// Bottom-Left
		};

		// Project frustum corners into world space
		for (uint32_t i = 0; i < 8; i++)
		{
			glm::vec4 invCorner = invViewProjection * glm::vec4(frustumCorners[i], 1.0f);
			// Perspective divide
			frustumCorners[i] = invCorner / invCorner.w;
		}

		float lastSplitDist = 0.f;
		for (uint32_t i = 0; i < shadowCascadesCount; i++)
		{
			float splitDist = cascadeSplits[i];

			glm::vec3 cascadeFrustrum[8];
			memcpy(cascadeFrustrum, frustumCorners, sizeof(glm::vec3) * 8.f);

			for (uint32_t i = 0; i < 4; i++)
			{
				// Distance between the front and the bottom
				glm::vec3 dist = cascadeFrustrum[i + 4] - cascadeFrustrum[i];
				// Ajust the back face base on the split distane
				cascadeFrustrum[i + 4] = cascadeFrustrum[i] + (dist * splitDist);
				// Ajust the front face base on the last split distane
				cascadeFrustrum[i] = cascadeFrustrum[i] + (dist * lastSplitDist);
			}

			// Calculate the frustum center (through the average of each corner)
			glm::vec3 frustumCenter = glm::vec3(0.0f);
			for (uint32_t i = 0; i < 8; i++)
				frustumCenter += cascadeFrustrum[i];

			frustumCenter /= 8.0f;

			float radius = 0.0f;
			for (uint32_t i = 0; i < 8; i++)
			{
				float distance = glm::length(cascadeFrustrum[i] - frustumCenter);
				radius = glm::max(radius, distance);
			}

			glm::vec3 maxExtents = glm::vec3(radius);
			glm::vec3 minExtents = -maxExtents;

			glm::vec3 lightDir = lightDirection;
			glm::vec3 eye = frustumCenter + lightDir * radius;
			glm::mat4 lightViewMatrix = glm::lookAt(eye, frustumCenter, glm::vec3(0.0f, 1.0f, .0f));
			glm::mat4 lightOrthoMatrix = math::MatFactory::CreateOrthographicProjectionMatrix(minExtents.x, maxExtents.x,
										minExtents.y, maxExtents.y,
										m_CascadeNearPlaneOffset,
										maxExtents.z - minExtents.z + m_CascadeFarPlaneOffset);
			// Offset to texel space to avoid shimmering (from https://stackoverflow.com/questions/33499053/cascaded-shadow-map-shimmering)
			glm::mat4 shadowMatrix = lightOrthoMatrix * lightViewMatrix;
			float ShadowMapResolution = (float)Renderer::GetRendererConfig().ShadowMapResolution;
			glm::vec4 shadowOrigin = (shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) * ShadowMapResolution / 2.0f;
			glm::vec4 roundedOrigin = glm::round(shadowOrigin);
			glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
			roundOffset = roundOffset * 2.0f / ShadowMapResolution;
			roundOffset.z = 0.0f;
			roundOffset.w = 0.0f;

			lightOrthoMatrix[3] += roundOffset;
		
			// Store split distance and matrix in cascade
			m_CascadedData[i].CascadeSplit = (minZ + splitDist * rangeZ) * -1.0f;
			m_CascadedData[i].ViewProjectionMatrix = lightOrthoMatrix * lightViewMatrix;
			m_CascadedData[i].ViewMatrix = lightViewMatrix;

			lastSplitDist = cascadeSplits[i];
		}
	}

}