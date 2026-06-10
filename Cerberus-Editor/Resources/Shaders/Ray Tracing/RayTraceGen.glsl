#type raygen
#version 450 core


layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Binormal;
layout(location = 4) in vec2 a_TexCoord;

struct VertexOutput
{
	vec3 WorldPosition;
	vec3 ViewPosition;
    vec3 Normal;
	vec2 TexCoord;
	mat3 WorldNormals;
	mat3 WorldTransform;
	vec3 Binormal;
};

layout(location = 0) out VertexOutput vs_Output;

layout(set = 1, std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjectionMatrix;
    mat4 u_ViewMatrix;
    mat4 u_ProjectionMatrix;
    mat4 u_InverseViewReverseProjectionMatrix;
    mat4 u_InverseViewMatrix;
    mat4 u_InverseProjectionMatrix;
};

layout(push_constant) uniform Transform
{
	mat4 Transform;
} u_Renderer;

void main()
{
	vs_Output.WorldPosition = vec3(u_Renderer.Transform * vec4(a_Position, 1.f));
	vs_Output.ViewPosition = vec3(u_ViewMatrix * vec4(vs_Output.WorldPosition, 1.f));
	vs_Output.TexCoord = vec2(a_TexCoord.x,  1 - a_TexCoord.y);
	vs_Output.Normal =  mat3(u_Renderer.Transform) * a_Normal;
	vs_Output.WorldTransform = mat3(u_Renderer.Transform);
	vs_Output.WorldNormals = vs_Output.WorldTransform * mat3(a_Tangent, a_Binormal, a_Normal);
	vs_Output.Binormal = a_Binormal;
	// Create vertex position
    gl_Position = u_ViewProjectionMatrix * vec4(vs_Output.WorldPosition, 1.f);
}