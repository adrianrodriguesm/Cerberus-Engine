#version 450

layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform u_Renderer
{
    mat4 WorldTransform;
    uint MaterialIndex;
    mat4 LightViewProjection;
} Draw;

void main()
{
    gl_Position = Draw.LightViewProjection * Draw.WorldTransform * vec4(a_Position, 1.0);
}
