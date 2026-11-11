#version 450

#include "../Common/VertexPulling.glsl"

// Leading block matches ForwardPBR.vert's/PreDepth.vert's shared shape
// (MaterialIndex and the Reserved padding are unused here) -- see
// DrawStreamPushConstants in GraphicsCommandList.cpp. LightViewProjection is
// pushed separately by RenderShadowPass (ShadowPass.cpp) at a fixed offset
// after this block; GLSL's 16-byte mat4 alignment is why that offset is 96,
// not 84.
layout(push_constant) uniform u_Renderer
{
    VertexFloatBuffer VertexBuffer;
    uvec2 Reserved0;
    mat4 WorldTransform;
    uint MaterialIndex;
    uint Reserved1a;
    uint Reserved1b;
    uint Reserved1c;
    mat4 LightViewProjection;
} Draw;

void main()
{
    SurfaceVertex vertex = LoadSurfaceVertex(Draw.VertexBuffer, gl_VertexIndex);
    gl_Position = Draw.LightViewProjection * Draw.WorldTransform * vec4(vertex.Position, 1.0);
}
