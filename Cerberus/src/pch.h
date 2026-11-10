#pragma once

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <string>
#include <sstream>

#ifdef _WIN32
	// Pull in windows.h before GLFW/Vulkan so its APIENTRY definition wins;
	// GLFW only defines its own fallback when windows.h hasn't already (glfw3.h:121),
	// avoiding the C4005 redefinition warning minwindef.h triggers when GLFW defines it first.
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_SSE42   // or GLM_FORCE_SSE42 if your processor supports it
#define GLM_FORCE_INLINE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <Core/Base.h>