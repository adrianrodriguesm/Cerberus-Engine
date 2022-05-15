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

layout (binding = 0) uniform sampler2D u_Texture;

layout(push_constant) uniform Uniforms
{
	float Exposure;
} u_Uniforms;

// Based on http://www.oscars.org/science-technology/sci-tech-projects/aces
vec3 ACESTonemap(vec3 color)
{
	mat3 m1 = mat3( 0.59719, 0.07600, 0.02840,
					0.35458, 0.90834, 0.13383,
					0.04823, 0.01566, 0.83777
				  );
	mat3 m2 = mat3( 1.60475, -0.10208, -0.00327,
					-0.53108, 1.10813, -0.07276,
					-0.07367, -0.00605, 1.07602
				  );
	vec3 v = m1 * color;
	vec3 a = v * (v + 0.0245786) - 0.000090537;
	vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
	return clamp(m2 * (a / b), 0.0, 1.0);
}

vec3 GammaCorrect(vec3 color, float gamma)
{
	return pow(color, vec3(1.0f / gamma));
}

void main()
{
	const float gamma     = 2.2;
	const float pureWhite = 1.0;

	vec3 color = texture(u_Texture, Input.TexCoord).rgb;
	/**/
	color *= u_Uniforms.Exposure;
	color = ACESTonemap(color);
	/**/
	color = GammaCorrect(color.rgb, gamma);
	o_FragmentColor = vec4(color, 1.0);
}
