#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "Payload.glsl"
#include "Environment.glsl"

layout(location = 0) rayPayloadInEXT HitPayload Payload;

void main()
{
    vec3 direction = normalize(gl_WorldRayDirectionEXT);
    Payload.Emission = textureLod(
        EnvironmentRadiance,
        direction,
        SkyLight.Parameters.y).rgb * SkyLight.Parameters.x;
    Payload.Hit = 0u;
}
