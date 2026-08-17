#ifndef CERBERUS_RT_GEOMETRY_GLSL
#define CERBERUS_RT_GEOMETRY_GLSL

#extension GL_EXT_buffer_reference2 : require

// MeshSource::Vertex is a tightly packed stream of 14 floats:
// position, normal, tangent, binormal and UV.
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

const uint CERBERUS_VERTEX_STRIDE_FLOATS = 14u;

SurfaceVertex LoadSurfaceVertex(VertexFloatBuffer vertexBuffer, uint vertexIndex)
{
    uint base = vertexIndex * CERBERUS_VERTEX_STRIDE_FLOATS;

    SurfaceVertex vertex;
    vertex.Position = vec3(
        vertexBuffer.Values[base + 0u],
        vertexBuffer.Values[base + 1u],
        vertexBuffer.Values[base + 2u]);
    vertex.Normal = vec3(
        vertexBuffer.Values[base + 3u],
        vertexBuffer.Values[base + 4u],
        vertexBuffer.Values[base + 5u]);
    vertex.Tangent = vec3(
        vertexBuffer.Values[base + 6u],
        vertexBuffer.Values[base + 7u],
        vertexBuffer.Values[base + 8u]);
    vertex.Binormal = vec3(
        vertexBuffer.Values[base + 9u],
        vertexBuffer.Values[base + 10u],
        vertexBuffer.Values[base + 11u]);
    vertex.TexCoord = vec2(
        vertexBuffer.Values[base + 12u],
        vertexBuffer.Values[base + 13u]);
    return vertex;
}

uvec3 LoadTriangleIndices(
    IndexUIntBuffer indexBuffer,
    uint firstIndex,
    uint baseVertex,
    uint primitiveIndex)
{
    uint triangleFirstIndex = firstIndex + primitiveIndex * 3u;
    return uvec3(
        indexBuffer.Values[triangleFirstIndex + 0u],
        indexBuffer.Values[triangleFirstIndex + 1u],
        indexBuffer.Values[triangleFirstIndex + 2u]) + uvec3(baseVertex);
}

#endif // CERBERUS_RT_GEOMETRY_GLSL
