#pragma once

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <string>
#include <sstream> 

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_SSE42   // or GLM_FORCE_SSE42 if your processor supports it
#define GLM_FORCE_INLINE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <Core/Base.h>