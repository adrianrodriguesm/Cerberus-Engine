#ifndef CERBERUS_COMMON_INSTANCE_DATA_GLSL
#define CERBERUS_COMMON_INSTANCE_DATA_GLSL

#extension GL_EXT_buffer_reference2 : require

// Mirrors cerberus::GPUInstanceData (RenderScene.h) exactly -- one entry per
// raster mesh instance, indexed by gl_InstanceIndex. See
// docs/tasks/T4-instances-indirect.md.
struct GPUInstanceData
{
    mat4 WorldTransform;
    mat4 NormalTransform; // inverse-transpose, computed on the CPU -- no per-vertex inverse()
    vec4 WorldBoundingSphere; // xyz center, w radius; for GPU culling (T5), unused here
    uint MaterialIndex;
    uint VertexOffset; // global, from GeometryPool
    uint FirstIndex;   // global
    uint IndexCount;
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer InstanceDataBuffer
{
    GPUInstanceData Values[];
};

#endif // CERBERUS_COMMON_INSTANCE_DATA_GLSL
