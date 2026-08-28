#type rayGen
#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 1) uniform accelerationStructureEXT TLAS;
layout(rgba32f, set = 0, binding = 16) writeonly uniform image2D RTOutput;

layout(set = 0, binding = 2) uniform CameraData
{
    mat4 Projection;
    mat4 View;
    mat4 ViewProjection;
    mat4 InverseView;
    mat4 InverseProjection;
    mat4 InverseViewReverseProjection;

    vec4 Position;
    vec4 Forward;
    vec4 Right;
    vec4 Up;
    vec4 Params;
} Camera;

struct HitPayload
{
    vec3 WorldPosition;
    float HitT;
    vec3 ShadingNormal;
    float Roughness;
    vec3 GeometricNormal;
    float Metalness;
    vec3 Albedo;
    uint MaterialIndex;
    vec3 Emission;
    uint Hit;
    vec2 TexCoord;
    vec2 Padding;
};

layout(location = 0) rayPayloadEXT HitPayload Payload;

vec3 GetCameraRayDirection(vec2 launchId, vec2 launchSize)
{
    vec2 uv = (launchId + vec2(0.5)) / launchSize;
    vec2 screen = uv * 2.0 - 1.0;

    float tanHalfFovY = Camera.Params.x;
    float aspect = Camera.Params.y;

    return normalize(
        Camera.Forward.xyz +
        Camera.Right.xyz * screen.x * aspect * tanHalfFovY +
        Camera.Up.xyz * screen.y * tanHalfFovY
    );
}

void main()
{
    ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);

    Payload.Emission = vec3(0.0);
    Payload.Hit = 0u;

    traceRayEXT(
        TLAS,
        gl_RayFlagsOpaqueEXT,
        0xFF,
        0,
        0,
        0,
        Camera.Position.xyz,
        0.001,
        GetCameraRayDirection(vec2(gl_LaunchIDEXT.xy), vec2(gl_LaunchSizeEXT.xy)),
        100000.0,
        0
    );

    vec3 radiance = Payload.Hit != 0u
        ? Payload.Albedo + Payload.Emission
        : Payload.Emission;

    imageStore(RTOutput, pixel, vec4(radiance, 1.0));
}

#type miss
#version 460

#extension GL_EXT_ray_tracing : require

struct HitPayload
{
    vec3 WorldPosition;
    float HitT;
    vec3 ShadingNormal;
    float Roughness;
    vec3 GeometricNormal;
    float Metalness;
    vec3 Albedo;
    uint MaterialIndex;
    vec3 Emission;
    uint Hit;
    vec2 TexCoord;
    vec2 Padding;
};

layout(location = 0) rayPayloadInEXT HitPayload Payload;

void main()
{
    Payload.Emission = vec3(0.02, 0.04, 0.08);
    Payload.Hit = 0u;
}

#type closestHit
#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_nonuniform_qualifier : require

struct HitPayload
{
    vec3 WorldPosition;
    float HitT;
    vec3 ShadingNormal;
    float Roughness;
    vec3 GeometricNormal;
    float Metalness;
    vec3 Albedo;
    uint MaterialIndex;
    vec3 Emission;
    uint Hit;
    vec2 TexCoord;
    vec2 Padding;
};

struct GPUMaterialData
{
    vec4 AlbedoColor;
    vec4 Parameters;
    uvec4 TextureIndices;
};

layout(std430, set = 0, binding = 3) readonly buffer MaterialBuffer
{
    GPUMaterialData Materials[];
} MaterialTable;

const uint CERBERUS_INVALID_TEXTURE_INDEX = 0xFFFFFFFFu;
layout(set = 0, binding = 5) uniform sampler2D RTTextures[];

bool IsValidRTTextureIndex(uint textureIndex)
{
	return textureIndex != CERBERUS_INVALID_TEXTURE_INDEX;
}

vec4 SampleRTTexture(uint textureIndex, vec2 texCoord)
{
    vec2 samplingUV = vec2(texCoord.x, 1.0 - texCoord.y);
    return textureLod(RTTextures[nonuniformEXT(textureIndex)], samplingUV, 0.0);
}

layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer VertexFloatBuffer
{
    float Values[];
};

layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer IndexUIntBuffer
{
    uint Values[];
};

struct SurfaceVertex
{
    vec3 Position;
    vec3 Normal;
    vec3 Tangent;
    vec3 Binormal;
    vec2 TexCoord;
};

SurfaceVertex LoadSurfaceVertex(VertexFloatBuffer vertexBuffer, uint vertexIndex)
{
    uint base = vertexIndex * 14u;
    SurfaceVertex vertex;
    vertex.Position = vec3(vertexBuffer.Values[base + 0u], vertexBuffer.Values[base + 1u], vertexBuffer.Values[base + 2u]);
    vertex.Normal = vec3(vertexBuffer.Values[base + 3u], vertexBuffer.Values[base + 4u], vertexBuffer.Values[base + 5u]);
    vertex.Tangent = vec3(vertexBuffer.Values[base + 6u], vertexBuffer.Values[base + 7u], vertexBuffer.Values[base + 8u]);
    vertex.Binormal = vec3(vertexBuffer.Values[base + 9u], vertexBuffer.Values[base + 10u], vertexBuffer.Values[base + 11u]);
    vertex.TexCoord = vec2(vertexBuffer.Values[base + 12u], vertexBuffer.Values[base + 13u]);
    return vertex;
}

uvec3 LoadTriangleIndices(IndexUIntBuffer indexBuffer, uint firstIndex, uint baseVertex, uint primitiveIndex)
{
    uint triangleFirstIndex = firstIndex + primitiveIndex * 3u;
    return uvec3(
        indexBuffer.Values[triangleFirstIndex + 0u],
        indexBuffer.Values[triangleFirstIndex + 1u],
        indexBuffer.Values[triangleFirstIndex + 2u]) + uvec3(baseVertex);
}

struct RayTracingInstanceData
{
    VertexFloatBuffer VertexBuffer;
    IndexUIntBuffer IndexBuffer;
    uint MaterialIndex;
    uint FirstIndex;
    uint BaseVertex;
    uint Reserved;
};

layout(std430, set = 0, binding = 4) readonly buffer InstanceBuffer
{
    RayTracingInstanceData Instances[];
} InstanceTable;

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
}
