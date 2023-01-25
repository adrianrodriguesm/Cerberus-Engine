#type vertex
#version 450 core

struct OutputBlock
{
	vec2 TexCoord;
};

layout (location = 0) out OutputBlock Output;

layout(std140, set = 3, binding = 0) uniform RatTracingData
{
	mat4 u_Matrix;
};

void main()
{
	// Create texture coordenate
	Output.TexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	// Create vertex position
    gl_Position = u_Matrix * vec4(Output.TexCoord * 2.0f + -1.0f, 0.0f, 1.0f);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 o_FragmentColor;

struct OutputBlock
{
	vec2 TexCoord;
};

layout (location = 0) in OutputBlock Input;



void main()
{
	o_FragmentColor = vec4(1.f, 0.f, 0.f, 1.0);
}
