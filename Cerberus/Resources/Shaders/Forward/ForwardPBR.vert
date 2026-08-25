#version 450

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Binormal;
layout(location = 4) in vec2 a_TexCoord;

layout(std140, set = 0, binding = 0) uniform CameraData
{
    mat4 ViewProjection;
    mat4 InverseViewProjection;
    vec4 CameraPosition;
    vec4 Presentation;
} Camera;

layout(push_constant) uniform u_Renderer
{
    mat4 WorldTransform;
    uint MaterialIndex;
} Draw;

layout(location = 0) out vec3 v_WorldPosition;
layout(location = 1) out vec3 v_WorldNormal;
layout(location = 2) out vec3 v_WorldTangent;
layout(location = 3) out vec3 v_WorldBinormal;
layout(location = 4) out vec2 v_TexCoord;
layout(location = 5) flat out uint v_MaterialIndex;

void main()
{
    vec4 worldPosition = Draw.WorldTransform * vec4(a_Position, 1.0);
    // Non-uniform scale needs the inverse-transpose; computed here instead of
    // CPU-side so every DrawStream consumer can share one push-constant shape.
    mat3 normalMatrix = transpose(inverse(mat3(Draw.WorldTransform)));

    v_WorldPosition = worldPosition.xyz;
    v_WorldNormal = normalize(normalMatrix * a_Normal);
    v_WorldTangent = normalize(mat3(Draw.WorldTransform) * a_Tangent);
    v_WorldBinormal = normalize(mat3(Draw.WorldTransform) * a_Binormal);
    v_TexCoord = a_TexCoord;
    v_MaterialIndex = Draw.MaterialIndex;
    gl_Position = Camera.ViewProjection * worldPosition;
}
