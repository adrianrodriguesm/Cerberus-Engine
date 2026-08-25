#include "pch.h"
#include "Renderer/SceneRenderer/Forward/Passes/ShadowPass.h"

#include "Renderer/RHI/Command/GraphicsCommandList.h"
#include "Renderer/Core/Renderer.h"
#include "Math/Matrix.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>

namespace cerberus
{
	namespace
	{
		// Offset of Shadow.vert's LightViewProjection push-constant member.
		// ExecuteDrawStream pushes the shared { WorldTransform, MaterialIndex }
		// block (68 bytes) at offset 0; mat4's 16-byte alignment pads the next
		// member out to byte 80, not 68.
		constexpr uint32_t LightViewProjectionOffset = 80;
	}

	std::array<ShadowCascadeData, ShadowCascadeCount> ComputeShadowCascades(
		float cameraNear,
		float cameraFar,
		const glm::mat4& inverseViewProjection,
		const glm::vec3& lightDirection,
		float cascadeSplitLambda,
		float nearPlaneOffset,
		float farPlaneOffset)
	{
		std::array<ShadowCascadeData, ShadowCascadeCount> cascades{};

		const float rangeZ = cameraFar - cameraNear;
		const float ratio = cameraFar / cameraNear;

		// Practical split scheme: blend logarithmic (good perspective sampling
		// density near the camera) and uniform (good far-plane density) splits.
		// https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
		std::array<float, ShadowCascadeCount> cascadeSplits{};
		for (uint32_t i = 0; i < ShadowCascadeCount; ++i)
		{
			const float portion = (i + 1) / float(ShadowCascadeCount);
			const float logSplit = cameraNear * std::pow(ratio, portion);
			const float uniformSplit = cameraNear + rangeZ * portion;
			const float splitDistance = cascadeSplitLambda * logSplit + (1.0f - cascadeSplitLambda) * uniformSplit;
			cascadeSplits[i] = (splitDistance - cameraNear) / rangeZ;
		}

		glm::vec3 frustumCorners[8] = {
			// Near (negative Z in clip space)
			glm::vec3(-1.0f,  1.0f, -1.0f), glm::vec3(1.0f,  1.0f, -1.0f),
			glm::vec3(1.0f, -1.0f, -1.0f), glm::vec3(-1.0f, -1.0f, -1.0f),
			// Far (positive Z in clip space)
			glm::vec3(-1.0f,  1.0f,  1.0f), glm::vec3(1.0f,  1.0f,  1.0f),
			glm::vec3(1.0f, -1.0f,  1.0f), glm::vec3(-1.0f, -1.0f,  1.0f),
		};
		for (glm::vec3& corner : frustumCorners)
		{
			const glm::vec4 worldCorner = inverseViewProjection * glm::vec4(corner, 1.0f);
			corner = glm::vec3(worldCorner) / worldCorner.w;
		}

		float lastSplitDistance = 0.0f;
		for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowCascadeCount; ++cascadeIndex)
		{
			const float splitDistance = cascadeSplits[cascadeIndex];

			glm::vec3 cascadeCorners[8];
			std::memcpy(cascadeCorners, frustumCorners, sizeof(cascadeCorners));
			for (uint32_t i = 0; i < 4; ++i)
			{
				const glm::vec3 cornerDistance = cascadeCorners[i + 4] - cascadeCorners[i];
				cascadeCorners[i + 4] = cascadeCorners[i] + cornerDistance * splitDistance;
				cascadeCorners[i] = cascadeCorners[i] + cornerDistance * lastSplitDistance;
			}

			glm::vec3 frustumCenter(0.0f);
			for (const glm::vec3& corner : cascadeCorners)
				frustumCenter += corner;
			frustumCenter /= 8.0f;

			float radius = 0.0f;
			for (const glm::vec3& corner : cascadeCorners)
				radius = glm::max(radius, glm::length(corner - frustumCenter));

			// Quantize so tiny frame-to-frame float differences (camera
			// micro-movement, matrix-math noise) don't change the ortho scale.
			// Without this, the world-space size of one shadow-map texel shifts
			// every frame even though the texel-origin snapping below assumes a
			// fixed scale, which is what produces visible shimmering/flicker.
			radius = std::ceil(radius * 16.0f) / 16.0f;

			const glm::vec3 maxExtents(radius);
			const glm::vec3 minExtents = -maxExtents;

			const glm::vec3 eye = frustumCenter + lightDirection * radius;
			const glm::mat4 lightView = glm::lookAt(eye, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
			glm::mat4 lightProjection = math::MatFactory::CreateOrthographicProjectionMatrix(
				minExtents.x, maxExtents.x,
				minExtents.y, maxExtents.y,
				nearPlaneOffset,
				maxExtents.z - minExtents.z + farPlaneOffset);

			// Snap the light-space origin to texel-sized increments so the cascade
			// doesn't shimmer as the frustum-fitted bounds move with the camera.
			// https://stackoverflow.com/questions/33499053/cascaded-shadow-map-shimmering
			const float shadowMapResolution = float(Renderer::GetRendererConfig().ShadowMapResolution);
			const glm::mat4 shadowMatrix = lightProjection * lightView;
			glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) * shadowMapResolution * 0.5f;
			const glm::vec4 roundedOrigin = glm::round(shadowOrigin);
			glm::vec4 roundOffset = (roundedOrigin - shadowOrigin) * 2.0f / shadowMapResolution;
			roundOffset.z = 0.0f;
			roundOffset.w = 0.0f;
			lightProjection[3] += roundOffset;

			cascades[cascadeIndex].ViewProjection = lightProjection * lightView;
			cascades[cascadeIndex].SplitDepth = (cameraNear + splitDistance * rangeZ) * -1.0f;

			lastSplitDistance = cascadeSplits[cascadeIndex];
		}

		return cascades;
	}

	void RenderShadowPass(
		GraphicsCommandList& commandList,
		const ShadowPassResources& resources,
		const ShadowPassParameters& parameters,
		const ShadowCascadeData& cascade)
	{
		if (parameters.DrawCount == 0)
			return;

		// Bind explicitly (rather than relying on ExecuteDrawStream's internal
		// bind) so the cascade's light matrix can be pushed once, before the
		// per-draw {WorldTransform, MaterialIndex} pushes inside the stream replay.
		commandList.BindGraphicsPipeline(resources.Pipeline);
		commandList.PushConstants(
			VK_SHADER_STAGE_VERTEX_BIT,
			AsBytes(cascade.ViewProjection),
			LightViewProjectionOffset);

		DrawArea drawArea;
		drawArea.GraphicsPipeline = resources.Pipeline;
		drawArea.DrawCount = parameters.DrawCount;

		commandList.ExecuteDrawStream(drawArea, parameters.DrawStream);
	}
}
