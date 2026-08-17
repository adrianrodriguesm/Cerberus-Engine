#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "Payload.glsl"
#include "Material.glsl"

hitAttributeEXT vec2 HitAttribs;
layout(location = 0) rayPayloadInEXT HitPayload Payload;

void main()
{
    vec3 barycentric = vec3(
        1.0 - HitAttribs.x - HitAttribs.y,
        HitAttribs.x,
        HitAttribs.y
    );

    // Temporary material validation path.
    // 2D.5 will resolve the correct material from hit geometry/submesh data.
    GPUMaterialData material = MaterialTable.Materials[0];
    vec3 baseColor = material.AlbedoColor.rgb;
    float emission = material.Parameters.z;

    Payload.hitValue = baseColor * (0.25 + 0.75 * barycentric) + vec3(emission);
}
