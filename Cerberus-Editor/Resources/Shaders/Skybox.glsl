// Skybox shader

#type vertex
#version 450 core
/** /
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;
/**/
layout(binding = 0) uniform Camera
{
    mat4 u_ViewProjectionMatrix;
    mat4 u_ViewMatrix;
    mat4 u_ProjectionMatrix;
    mat4 u_InverseViewReverseProjectionMatrix;
    mat4 u_InverseViewMatrix;
    mat4 u_InverseProjectionMatrix;
};


layout (location = 0) out vec3 o_Position;

void main()
{
    // Create texture coordenate
	vec2 texCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	// Create vertex position from texCoord 
    vec4 position = vec4(texCoord * 2.0f + -1.0f, 0.0f, 1.f);
    // Set Vertex Position
	gl_Position = position;
	o_Position = (u_InverseViewReverseProjectionMatrix * position).xyz;

}

#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;

layout (binding = 1) uniform samplerCube u_Texture;

layout (push_constant) uniform Uniforms
{
	int TextureLod;
	float Intensity;
} u_MaterialUniforms;

layout (location = 0) in vec3 v_Position;

void main()
{
    o_Color = textureLod(u_Texture, normalize(v_Position), u_MaterialUniforms.TextureLod) * u_MaterialUniforms.Intensity;
}
