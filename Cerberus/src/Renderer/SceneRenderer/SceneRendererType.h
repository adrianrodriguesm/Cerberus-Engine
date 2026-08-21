#pragma once

#include <cstdint>
#include <string_view>

namespace cerberus
{
	enum class SceneRendererType : uint8_t
	{
		Forward = 0,
		PathTracer
	};

	inline constexpr std::string_view SceneRendererTypeToString(SceneRendererType type)
	{
		switch (type)
		{
		case SceneRendererType::Forward:    return "Forward";
		case SceneRendererType::PathTracer: return "PathTracer";
		}

		return "PathTracer";
	}

	inline constexpr SceneRendererType SceneRendererTypeFromString(std::string_view type)
	{
		return type == "Forward"
			? SceneRendererType::Forward
			: SceneRendererType::PathTracer;
	}
}
