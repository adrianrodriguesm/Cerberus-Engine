#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "Payload.glsl"

hitAttributeEXT vec2 HitAttribs;
layout(location = 0) rayPayloadInEXT HitPayload Payload;

void main()
{
    vec3 barycentric = vec3(
        1.0 - HitAttribs.x - HitAttribs.y,
        HitAttribs.x,
        HitAttribs.y
    );

    // Basic barycentric visualization. This keeps the shader independent from
    // vertex/index buffers until the object-data system is ready.
    Payload.hitValue = barycentric;
}
