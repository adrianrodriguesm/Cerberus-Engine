#ifndef CERBERUS_RT_DIRECTIONAL_LIGHTS_GLSL
#define CERBERUS_RT_DIRECTIONAL_LIGHTS_GLSL

const uint CERBERUS_MAX_DIRECTIONAL_LIGHTS = 4u;

struct DirectionalLightData
{
    // xyz = direction from the surface toward the light, w = intensity.
    vec4 DirectionAndIntensity;
    vec4 Radiance;
};

layout(std140, set = 0, binding = 9) uniform DirectionalLightBuffer
{
    // x = active light count, y/z/w = unused.
    uvec4 Parameters;
    DirectionalLightData Lights[CERBERUS_MAX_DIRECTIONAL_LIGHTS];
} DirectionalLights;

#endif // CERBERUS_RT_DIRECTIONAL_LIGHTS_GLSL
