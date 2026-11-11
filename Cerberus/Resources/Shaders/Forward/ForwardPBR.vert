#version 450

#include "../Common/VertexPulling.glsl"

layout(std140, set = 0, binding = 0) uniform CameraData
{
    mat4 ViewProjection;
    mat4 InverseViewProjection;
    vec4 CameraPosition;
    vec4 Presentation;
} Camera;

// Reserved0/Reserved1* are unused padding -- see DrawStreamPushConstants in
// GraphicsCommandList.cpp for why they have to be here (GLSL's 16-byte mat4
// alignment, plus padding the whole block out to a 16-byte boundary for
// Shadow.vert's separate trailing LightViewProjection push).
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
    SurfaceVertex vertex = LoadSurfaceVertex(Draw.VertexBuffer, gl_VertexIndex);

    vec4 worldPosition = Draw.WorldTransform * vec4(vertex.Position, 1.0);
    // Non-uniform scale needs the inverse-transpose; computed here instead of
    // CPU-side so every DrawStream consumer can share one push-constant shape.
    mat3 normalMatrix = transpose(inverse(mat3(Draw.WorldTransform)));

    v_WorldPosition = worldPosition.xyz;
    v_WorldNormal = normalize(normalMatrix * vertex.Normal);
    v_WorldTangent = normalize(mat3(Draw.WorldTransform) * vertex.Tangent);
    v_WorldBinormal = normalize(mat3(Draw.WorldTransform) * vertex.Binormal);
    v_TexCoord = vertex.TexCoord;
    v_MaterialIndex = Draw.MaterialIndex;
    gl_Position = Camera.ViewProjection * worldPosition;
}
