#ifndef CERBERUS_RT_ENVIRONMENT_GLSL
#define CERBERUS_RT_ENVIRONMENT_GLSL

layout(set = 0, binding = 6) uniform samplerCube EnvironmentRadiance;

layout(std140, set = 0, binding = 7) uniform SkyLightData
{
    // x = intensity, y = radiance mip LOD, z/w = unused.
    vec4 Parameters;
} SkyLight;

#endif // CERBERUS_RT_ENVIRONMENT_GLSL
