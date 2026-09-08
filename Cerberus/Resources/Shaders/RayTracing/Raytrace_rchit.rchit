#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

#include "Payload.glsl"
#include "Material.glsl"
#include "TextureTable.glsl"
#include "Instance.glsl"

// TextureTable owns the RT-specific UV-origin conversion used by every map.

hitAttributeEXT vec2 HitAttribs;
layout(location = 0) rayPayloadInEXT HitPayload Payload;

void main()
{
    vec3 barycentric = vec3(
        1.0 - HitAttribs.x - HitAttribs.y,
        HitAttribs.x,
        HitAttribs.y
    );

    uint instanceIndex = uint(gl_InstanceCustomIndexEXT);
    RayTracingInstanceData instance = InstanceTable.Instances[instanceIndex];

    uvec3 triangleIndices = LoadTriangleIndices(
        instance.IndexBuffer,
        instance.FirstIndex,
        instance.BaseVertex,
        uint(gl_PrimitiveID));

    SurfaceVertex vertex0 = LoadSurfaceVertex(instance.VertexBuffer, triangleIndices.x);
    SurfaceVertex vertex1 = LoadSurfaceVertex(instance.VertexBuffer, triangleIndices.y);
    SurfaceVertex vertex2 = LoadSurfaceVertex(instance.VertexBuffer, triangleIndices.z);

    vec3 localPosition =
        vertex0.Position * barycentric.x +
        vertex1.Position * barycentric.y +
        vertex2.Position * barycentric.z;

    vec3 localShadingNormal = normalize(
        vertex0.Normal * barycentric.x +
        vertex1.Normal * barycentric.y +
        vertex2.Normal * barycentric.z);

    vec3 localTangent =
        vertex0.Tangent * barycentric.x +
        vertex1.Tangent * barycentric.y +
        vertex2.Tangent * barycentric.z;

    vec3 localBinormal =
        vertex0.Binormal * barycentric.x +
        vertex1.Binormal * barycentric.y +
        vertex2.Binormal * barycentric.z;

    vec3 localGeometricNormal = normalize(cross(
        vertex1.Position - vertex0.Position,
        vertex2.Position - vertex0.Position));

    vec2 texCoord =
        vertex0.TexCoord * barycentric.x +
        vertex1.TexCoord * barycentric.y +
        vertex2.TexCoord * barycentric.z;

    mat3 normalToWorld = transpose(mat3(gl_WorldToObjectEXT));
    vec3 worldShadingNormal = normalize(normalToWorld * localShadingNormal);
    vec3 worldGeometricNormal = normalize(normalToWorld * localGeometricNormal);

    // Keep both normals in the hemisphere opposing the incoming path segment.
    if (dot(worldGeometricNormal, gl_WorldRayDirectionEXT) > 0.0)
        worldGeometricNormal = -worldGeometricNormal;
    if (dot(worldShadingNormal, worldGeometricNormal) < 0.0)
        worldShadingNormal = -worldShadingNormal;

    uint materialIndex = instance.MaterialIndex;
    GPUMaterialData material = MaterialTable.Materials[materialIndex];

    vec3 albedo = material.AlbedoColor.rgb;
    if (IsValidRTTextureIndex(material.TextureIndices.x))
        albedo *= SampleRTTexture(material.TextureIndices.x, texCoord).rgb;

    float roughness = material.Parameters.x;
    if (IsValidRTTextureIndex(material.TextureIndices.w))
        roughness *= SampleRTTexture(material.TextureIndices.w, texCoord).r;

    float metalness = material.Parameters.y;
    if (IsValidRTTextureIndex(material.TextureIndices.z))
        metalness *= SampleRTTexture(material.TextureIndices.z, texCoord).r;

    if (material.Parameters.w > 0.5 &&
        IsValidRTTextureIndex(material.TextureIndices.y) &&
        dot(localTangent, localTangent) > 1e-8 &&
        dot(localBinormal, localBinormal) > 1e-8)
    {
        mat3 objectToWorld = mat3(gl_ObjectToWorldEXT);
        vec3 worldTangent = objectToWorld * localTangent;
        worldTangent -= worldShadingNormal * dot(worldTangent, worldShadingNormal);

        if (dot(worldTangent, worldTangent) > 1e-8)
        {
            worldTangent = normalize(worldTangent);

            vec3 referenceBinormal = normalize(objectToWorld * localBinormal);
            float tangentSign = dot(cross(worldShadingNormal, worldTangent), referenceBinormal) < 0.0 ? -1.0 : 1.0;
            vec3 worldBinormal = tangentSign * normalize(cross(worldShadingNormal, worldTangent));

            vec3 tangentNormal = SampleRTTexture(material.TextureIndices.y, texCoord).xyz * 2.0 - 1.0;
            worldShadingNormal = normalize(mat3(worldTangent, worldBinormal, worldShadingNormal) * tangentNormal);

            if (dot(worldShadingNormal, worldGeometricNormal) < 0.0)
                worldShadingNormal = -worldShadingNormal;
        }
    }

    // Closest-hit only reconstructs the surface. The path integration and BSDF
    // sampling belong to raygen, which will consume this payload per bounce.
    Payload.WorldPosition = gl_ObjectToWorldEXT * vec4(localPosition, 1.0);
    Payload.HitT = gl_HitTEXT;
    Payload.ShadingNormal = worldShadingNormal;
    Payload.Roughness = clamp(roughness, 0.0, 1.0);
    Payload.GeometricNormal = worldGeometricNormal;
    Payload.Metalness = clamp(metalness, 0.0, 1.0);
    Payload.Albedo = albedo;
    Payload.MaterialIndex = materialIndex;
    Payload.Emission = vec3(max(material.Parameters.z, 0.0));
    Payload.Hit = 1u;
    Payload.TexCoord = texCoord;
    Payload.InstanceIndex = instanceIndex;
}
