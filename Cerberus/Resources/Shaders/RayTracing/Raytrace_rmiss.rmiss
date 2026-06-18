#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "Payload.glsl"

layout(location = 0) rayPayloadInEXT HitPayload Payload;

void main()
{
    Payload.hitValue = vec3(0.02, 0.04, 0.08);
}
