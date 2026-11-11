#ifndef CERBERUS_RT_GEOMETRY_GLSL
#define CERBERUS_RT_GEOMETRY_GLSL

#include "../Common/VertexPulling.glsl"

layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer IndexUIntBuffer
{
    uint Values[];
};

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
