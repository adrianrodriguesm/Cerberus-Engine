#version 450

struct DirectionalLight
{
    vec4 DirectionAndIntensity;
    vec4 Radiance;
};

layout(std140, set = 0, binding = 0) uniform CameraData
{
    mat4 ViewProjection;
    mat4 InverseViewProjection;
    vec4 CameraPosition;
    vec4 Presentation;
} Camera;

layout(set = 0, binding = 3) uniform samplerCube EnvironmentRadiance;

layout(std140, set = 0, binding = 5) uniform ForwardLightingData
{
    vec4 SkyLightParameters;
    uvec4 DirectionalLightParameters;
    DirectionalLight DirectionalLights[4];
} Lighting;

layout(location = 0) in vec2 v_NDC;
layout(location = 0) out vec4 o_Color;

void main()
{
    vec4 worldPoint = Camera.InverseViewProjection * vec4(v_NDC, 1.0, 1.0);
    worldPoint /= worldPoint.w;
    vec3 direction = normalize(worldPoint.xyz - Camera.CameraPosition.xyz);
    vec3 radiance = textureLod(
        EnvironmentRadiance,
        direction,
        max(Lighting.SkyLightParameters.y, 0.0)).rgb;
    radiance *= max(Lighting.SkyLightParameters.x, 0.0);
    // SceneColor stays linear HDR; tonemap/exposure/gamma happen once in the
    // Composite pass instead of being duplicated in every shading shader.
    o_Color = vec4(radiance, 1.0);
}
