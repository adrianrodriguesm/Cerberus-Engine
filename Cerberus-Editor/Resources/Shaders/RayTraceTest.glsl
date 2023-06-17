#type vertex
#version 450 core

struct OutputBlock
{
	vec2 TexCoord;
};

layout (location = 0) out OutputBlock Output;

void main()
{
	// Create texture coordenate
	Output.TexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	// Create vertex position
    gl_Position = vec4(Output.TexCoord * 2.0f + -1.0f, 0.0f, 1.0f);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 o_FragmentColor;

struct OutputBlock
{
	vec2 TexCoord;
};

layout (location = 0) in OutputBlock Input;
layout(set = 0, binding = 0) uniform sampler2D u_FullscreenTex;



void main()
{
	o_FragmentColor = texture(u_FullscreenTex, vec2(Input.TexCoord.x, 1 - Input.TexCoord.y));
}
