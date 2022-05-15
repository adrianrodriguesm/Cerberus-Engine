#pragma once
#include "Scene/EnvironmentMap.h"
namespace cerberus
{
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
		std::array<DirectionalLight, 4> DirectionalLights;
		SkyLight SkyLight;
	};

}