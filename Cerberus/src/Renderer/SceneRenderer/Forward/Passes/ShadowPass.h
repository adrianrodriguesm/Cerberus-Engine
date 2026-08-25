#pragma once

#include "Core/Handle.h"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <span>

namespace cerberus
{
	class GraphicsCommandList;
	class GraphicsPipeline;

	constexpr uint32_t ShadowCascadeCount = 4;

	struct ShadowCascadeData
	{
		glm::mat4 ViewProjection = glm::mat4(1.0f);
		float SplitDepth = 0.0f;
	};

	// Fits ShadowCascadeCount light-space orthographic frustums to slices of the
	// camera frustum (practical split scheme, GPU Gems 3 ch.10), with texel
	// snapping so cascade bounds don't shimmer as the camera moves.
	std::array<ShadowCascadeData, ShadowCascadeCount> ComputeShadowCascades(
		float cameraNear,
		float cameraFar,
		const glm::mat4& inverseViewProjection,
		const glm::vec3& lightDirection,
		float cascadeSplitLambda = 0.93f,
		float nearPlaneOffset = -15.0f,
		float farPlaneOffset = 0.0f);

	struct ShadowPassResources
	{
		Handle<GraphicsPipeline> Pipeline;
	};

	struct ShadowPassParameters
	{
		std::span<const uint32_t> DrawStream;
		uint32_t DrawCount = 0;
	};

	// Depth-only pass from one cascade's light-space view, called once per
	// cascade against the same DrawStream PreDepth/Forward use. The cascade's
	// ViewProjection is pushed as a constant rather than read from a uniform
	// buffer: a UBO updated between cascades within one frame would be
	// overwritten by the CPU before the GPU executes each cascade's draws,
	// since all four cascades are recorded (not executed) sequentially.
	void RenderShadowPass(
		GraphicsCommandList& commandList,
		const ShadowPassResources& resources,
		const ShadowPassParameters& parameters,
		const ShadowCascadeData& cascade);
}
