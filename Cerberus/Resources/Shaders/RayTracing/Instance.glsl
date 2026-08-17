#ifndef CERBERUS_RT_INSTANCE_GLSL
#define CERBERUS_RT_INSTANCE_GLSL

#include "Geometry.glsl"

// Must match cerberus::RayTracingInstanceData.
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

#endif // CERBERUS_RT_INSTANCE_GLSL
