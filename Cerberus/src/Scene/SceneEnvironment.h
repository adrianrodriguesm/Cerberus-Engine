#pragma once
#include "Scene/EnvironmentMap.h"

#include <array>
#include <cstdint>

namespace cerberus
{
	inline constexpr uint32_t MaxDirectionalLightCount = 4;

	struct DirectionalLight
	{
		glm::vec3 Direction = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Radiance = { 0.0f, 0.0f, 0.0f };
		float Multiplier = 0.0f;
	};

	struct SkyLight
	{
		Ref<EnvironmentMap> EnvironmentMap;
		float Multiplier;
		uint32_t SampleLOD;
	};

	struct LightEnvironment
	{
		std::array<DirectionalLight, MaxDirectionalLightCount> DirectionalLights;
		SkyLight SkyLight;
	};

}
