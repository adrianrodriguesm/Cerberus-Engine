#ifndef CERBERUS_RT_COMMON_GLSL
#define CERBERUS_RT_COMMON_GLSL

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 1) uniform accelerationStructureEXT TLAS;
layout(rgba32f, set = 0, binding = 16) writeonly uniform image2D RTOutput;
layout(rgba32f, set = 0, binding = 17) uniform image2D RTAccumulation;

const float CERBERUS_RAY_T_MIN = 0.001;
const float CERBERUS_RAY_T_MAX = 100000.0;

#endif // CERBERUS_RT_COMMON_GLSL
