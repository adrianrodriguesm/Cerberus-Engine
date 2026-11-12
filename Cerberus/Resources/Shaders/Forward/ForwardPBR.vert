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

// Same shape as PreDepth.vert's/Shadow.vert's -- see DrawStreamPushConstants
// in GraphicsCommandList.cpp. WorldTransform/MaterialIndex now live in
// GPUInstanceData, read through Instances via gl_InstanceIndex.
layout(push_constant) uniform u_Renderer
{
    VertexFloatBuffer VertexBuffer;
    InstanceDataBuffer Instances;
} Draw;

layout(location = 0) out vec3 v_WorldPosition;
layout(location = 1) out vec3 v_WorldNormal;
layout(location = 2) out vec3 v_WorldTangent;
layout(location = 3) out vec3 v_WorldBinormal;
layout(location = 4) out vec2 v_TexCoord;
layout(location = 5) flat out uint v_MaterialIndex;

// Must match PreDepth.vert's gl_Position expression exactly (same operand
// order too -- floating point is not associative) so PreDepth and Forward
// agree bit-for-bit and early-Z (LessOrEqual) behaves as intended.
invariant gl_Position;

void main()
{
    GPUInstanceData inst = Draw.Instances.Values[gl_InstanceIndex];
    SurfaceVertex vertex = LoadSurfaceVertex(Draw.VertexBuffer, gl_VertexIndex);

    vec4 worldPosition = inst.WorldTransform * vec4(vertex.Position, 1.0);
    mat3 normalMatrix = mat3(inst.NormalTransform);

    v_WorldPosition = worldPosition.xyz;
    v_WorldNormal = normalize(normalMatrix * vertex.Normal);
    v_WorldTangent = normalize(mat3(inst.WorldTransform) * vertex.Tangent);
    v_WorldBinormal = normalize(mat3(inst.WorldTransform) * vertex.Binormal);
    v_TexCoord = vertex.TexCoord;
    v_MaterialIndex = inst.MaterialIndex;
    gl_Position = Camera.ViewProjection * worldPosition;
}
