#version 450

#include "../Common/VertexPulling.glsl"
#include "../Common/InstanceData.glsl"

layout(std140, set = 0, binding = 0) uniform CameraData
{
    mat4 ViewProjection;
    mat4 InverseViewProjection;
    vec4 CameraPosition;
    vec4 Presentation;
} Camera;

// Same shape as ForwardPBR.vert's/Shadow.vert's push_constant block -- see
// DrawStreamPushConstants in GraphicsCommandList.cpp.
layout(push_constant) uniform u_Renderer
{
    VertexFloatBuffer VertexBuffer;
    InstanceDataBuffer Instances;
} Draw;

// Must match ForwardPBR.vert's gl_Position expression exactly (same operand
// order too -- floating point is not associative) so PreDepth and Forward
// agree bit-for-bit and early-Z (LessOrEqual) behaves as intended.
invariant gl_Position;

void main()
{
    GPUInstanceData inst = Draw.Instances.Values[gl_InstanceIndex];
    SurfaceVertex vertex = LoadSurfaceVertex(Draw.VertexBuffer, gl_VertexIndex);
    vec4 worldPosition = inst.WorldTransform * vec4(vertex.Position, 1.0);
    gl_Position = Camera.ViewProjection * worldPosition;
}
