#version 450

layout(location = 0) in vec3 a_Position;

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

void main()
{
    gl_Position = Camera.ViewProjection * Draw.WorldTransform * vec4(a_Position, 1.0);
}
