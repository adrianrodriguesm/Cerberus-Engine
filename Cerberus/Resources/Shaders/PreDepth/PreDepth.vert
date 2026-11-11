#version 450

#include "../Common/VertexPulling.glsl"

layout(std140, set = 0, binding = 0) uniform CameraData
{
    mat4 ViewProjection;
    mat4 InverseViewProjection;
    vec4 CameraPosition;
    vec4 Presentation;
} Camera;

// Same shape as ForwardPBR.vert's push_constant block (MaterialIndex and the
// Reserved padding are unused here) -- see its comment and
// DrawStreamPushConstants in GraphicsCommandList.cpp.
layout(push_constant) uniform u_Renderer
{
    VertexFloatBuffer VertexBuffer;
    uvec2 Reserved0;
    mat4 WorldTransform;
    uint MaterialIndex;
    uint Reserved1a;
    uint Reserved1b;
    uint Reserved1c;
} Draw;

// Must match ForwardPBR.vert's gl_Position expression exactly (same operand
// order too -- floating point is not associative) so PreDepth and Forward
// agree bit-for-bit and early-Z (LessOrEqual) behaves as intended.
invariant gl_Position;

void main()
{
    SurfaceVertex vertex = LoadSurfaceVertex(Draw.VertexBuffer, gl_VertexIndex);
    vec4 worldPosition = Draw.WorldTransform * vec4(vertex.Position, 1.0);
    gl_Position = Camera.ViewProjection * worldPosition;
}
