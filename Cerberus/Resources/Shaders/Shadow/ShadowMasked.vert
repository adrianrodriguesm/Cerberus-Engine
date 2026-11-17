#version 450

#include "../Common/VertexPulling.glsl"
#include "../Common/InstanceData.glsl"

// Leading block matches ForwardPBR.vert's/PreDepth.vert's shared shape --
// see DrawStreamPushConstants in GraphicsCommandList.cpp. LightViewProjection
// is pushed separately by RenderShadowPass (ShadowPass.cpp) at a fixed
// offset after this block (16, already a 16-byte boundary since both
// VertexBuffer and Instances are 8-byte buffer_reference handles).
layout(push_constant) uniform u_Renderer
{
    VertexFloatBuffer VertexBuffer;
    InstanceDataBuffer Instances;
    mat4 LightViewProjection;
} Draw;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) flat out uint v_MaterialIndex;

void main()
{
    GPUInstanceData inst = Draw.Instances.Values[gl_InstanceIndex];
    SurfaceVertex vertex = LoadSurfaceVertex(Draw.VertexBuffer, gl_VertexIndex);
    v_TexCoord = vertex.TexCoord;
    v_MaterialIndex = inst.MaterialIndex;
    gl_Position = Draw.LightViewProjection * inst.WorldTransform * vec4(vertex.Position, 1.0);
}
