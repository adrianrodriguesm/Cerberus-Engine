#type vertex
#version 450

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
	vec4 ShadowMapCoords[4];
};
layout(location = 0) out VertexOutput vs_Output;


layout(std140, binding = 0) uniform Camera
{
    mat4 u_ViewProjectionMatrix;
    mat4 u_ViewMatrix;
    mat4 u_ProjectionMatrix;
    mat4 u_InverseViewReverseProjectionMatrix;
    mat4 u_InverseViewMatrix;
    mat4 u_InverseProjectionMatrix;
};

layout(std140, binding = 3) uniform ShadowData
{
	mat4 u_LightMatrix[4];
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
	vs_Output.ShadowMapCoords[0] = u_LightMatrix[0] * vec4(vs_Output.WorldPosition, 1.0);
	vs_Output.ShadowMapCoords[1] = u_LightMatrix[1] * vec4(vs_Output.WorldPosition, 1.0);
	vs_Output.ShadowMapCoords[2] = u_LightMatrix[2] * vec4(vs_Output.WorldPosition, 1.0);
	vs_Output.ShadowMapCoords[3] = u_LightMatrix[3] * vec4(vs_Output.WorldPosition, 1.0);

	gl_Position = u_ViewProjectionMatrix * vec4(vs_Output.WorldPosition, 1.f);
}

#type fragment
#version 450


struct VertexOutput
{
	vec3 WorldPosition;
	vec3 ViewPosition;
    vec3 Normal;
	vec2 TexCoord;
	mat3 WorldNormals;
	mat3 WorldTransform;
	vec3 Binormal;
	vec4 ShadowMapCoords[4];
};

layout(location = 0) in VertexOutput vs_Input;

layout(location = 0) out vec4 o_FragmentColor;

// Material Textures
layout(set = 0, binding = 4) uniform sampler2D u_AlbedoTex;
layout(set = 0, binding = 5) uniform sampler2D u_NormalTex;
layout(set = 0, binding = 6) uniform sampler2D u_RoughnessTex;
layout(set = 0, binding = 7) uniform sampler2D u_MetallicTex;

// Environment maps
layout(set = 1, binding = 8) uniform samplerCube u_EnvRadianceTex;
layout(set = 1, binding = 9) uniform samplerCube u_EnvIrradianceTex;

// BRDF LUT
layout(set = 1, binding = 10) uniform sampler2D u_BRDFLUTTex;

// Shadow maps
layout(set = 1, binding = 11) uniform sampler2DArray u_ShadowMapTex;

layout(push_constant) uniform Material
{
	layout(offset = 64) vec3 AlbedoColor;
						float Metalness;
						float Roughness;
						bool UseNormalMap;
} u_MaterialUniforms;

const float Epsilon = 0.00001f;
const float PI = 3.14159265359f;
const int LightCount = 4;
// Constant normal incidence Fresnel factor for all dielectrics.
const vec3 Fdielectric = vec3(0.04f);
const float NEAR = .01f;

struct PBRParameters
{
	vec3 Albedo;
	float Roughness;
	float Metalness;
	vec3 Normal;
	vec3 View;
	float NdotV;
};

PBRParameters m_Params;

struct DirectionalLight
{
	vec4 Direction;
	vec4 Radience;
	float Intensity;
};

layout(std140, binding = 1) uniform Environment
{
	DirectionalLight u_DirectionalLights[LightCount];
	vec3 u_CameraPosition;
};


layout(std140, binding = 2) uniform RendererData
{
	uniform vec4 u_CascadeSplits;
	uniform bool u_ShowCascades;
	uniform bool u_SoftShadows;
	uniform float u_LightSize;
	uniform float u_MaxShadowDistance;
	uniform float u_ShadowFade;
	uniform bool u_CascadeFading;
	uniform float u_CascadeTransitionFade;
};

/////////////////////////////////////////////
// PCSS
/////////////////////////////////////////////
const vec2 PoissonDistribution[64] = vec2[](
	vec2(-0.884081, 0.124488),
	vec2(-0.714377, 0.027940),
	vec2(-0.747945, 0.227922),
	vec2(-0.939609, 0.243634),
	vec2(-0.985465, 0.045534),
	vec2(-0.861367, -0.136222),
	vec2(-0.881934, 0.396908),
	vec2(-0.466938, 0.014526),
	vec2(-0.558207, 0.212662),
	vec2(-0.578447, -0.095822),
	vec2(-0.740266, -0.095631),
	vec2(-0.751681, 0.472604),
	vec2(-0.553147, -0.243177),
	vec2(-0.674762, -0.330730),
	vec2(-0.402765, -0.122087),
	vec2(-0.319776, -0.312166),
	vec2(-0.413923, -0.439757),
	vec2(-0.979153, -0.201245),
	vec2(-0.865579, -0.288695),
	vec2(-0.243704, -0.186378),
	vec2(-0.294920, -0.055748),
	vec2(-0.604452, -0.544251),
	vec2(-0.418056, -0.587679),
	vec2(-0.549156, -0.415877),
	vec2(-0.238080, -0.611761),
	vec2(-0.267004, -0.459702),
	vec2(-0.100006, -0.229116),
	vec2(-0.101928, -0.380382),
	vec2(-0.681467, -0.700773),
	vec2(-0.763488, -0.543386),
	vec2(-0.549030, -0.750749),
	vec2(-0.809045, -0.408738),
	vec2(-0.388134, -0.773448),
	vec2(-0.429392, -0.894892),
	vec2(-0.131597, 0.065058),
	vec2(-0.275002, 0.102922),
	vec2(-0.106117, -0.068327),
	vec2(-0.294586, -0.891515),
	vec2(-0.629418, 0.379387),
	vec2(-0.407257, 0.339748),
	vec2(0.071650, -0.384284),
	vec2(0.022018, -0.263793),
	vec2(0.003879, -0.136073),
	vec2(-0.137533, -0.767844),
	vec2(-0.050874, -0.906068),
	vec2(0.114133, -0.070053),
	vec2(0.163314, -0.217231),
	vec2(-0.100262, -0.587992),
	vec2(-0.004942, 0.125368),
	vec2(0.035302, -0.619310),
	vec2(0.195646, -0.459022),
	vec2(0.303969, -0.346362),
	vec2(-0.678118, 0.685099),
	vec2(-0.628418, 0.507978),
	vec2(-0.508473, 0.458753),
	vec2(0.032134, -0.782030),
	vec2(0.122595, 0.280353),
	vec2(-0.043643, 0.312119),
	vec2(0.132993, 0.085170),
	vec2(-0.192106, 0.285848),
	vec2(0.183621, -0.713242),
	vec2(0.265220, -0.596716),
	vec2(-0.009628, -0.483058),
	vec2(-0.018516, 0.435703)
	);

float ShadowFade = 1.f;
vec2 SamplePoisson(int index)
{
	return PoissonDistribution[index % 64];
}
// Calculate the bias along the surface’s normal direction, proportional
// to the sine of the angle between the normal and the light
// ---------------------------------------------------------------------
// Samples generated for the light are almost never at the same locations as the screen 
// samples. When the light’s stored depth value is compared to the viewed surface’s depth, 
// the light’s value may be slightly lower than the surface’s, resulting in self-shadowing (Shadow Acne)
float GetShadowBias()
{
	const float MINIMUM_SHADOW_BIAS = 0.002f;
	vec3 lightDirection = vec3(u_DirectionalLights[0].Direction);
	// Bias that is proportional to the angle of the receiver to the light
	// More the surface tilts away from the light, the greater the bias grows
	float bias = max(MINIMUM_SHADOW_BIAS * (1.0 - dot(m_Params.Normal, lightDirection)), MINIMUM_SHADOW_BIAS);
	return bias;
}
// Determine the sample area through the average  
// distance of the occluders from the location
float SearchRegionRadiusUV(float zWorld, float uvLightSize)
{
	return uvLightSize * (zWorld - NEAR) / zWorld;
}

float FindBlockerDistance_DirectionalLight(sampler2DArray shadowMap, uint cascade, vec3 shadowCoords, float uvLightSize)
{
	float bias = GetShadowBias();

	int numBlockerSearchSamples = 64;
	int blockers = 0;
	float avgBlockerDistance = 0;

	float searchWidth = SearchRegionRadiusUV(shadowCoords.z, uvLightSize);
	for (int i = 0; i < numBlockerSearchSamples; i++)
	{
		float shadowMapDepth = textureLod(shadowMap, vec3((shadowCoords.xy * 0.5 + 0.5) + SamplePoisson(i) * searchWidth, cascade), 0).r;
		// If true the fragment is in shadow
		if (shadowMapDepth < (shadowCoords.z - bias))
		{
			blockers++;
			avgBlockerDistance += shadowMapDepth;
		}
	}

	if (blockers > 0)
		return avgBlockerDistance / float(blockers);

	return -1;
}

float PCF_DirectionalLight(sampler2DArray shadowMap, uint cascade, vec3 shadowCoords, float uvRadius)
{
	float bias = GetShadowBias();
	int numPCFSamples = 64;

	float sum = 0;
	for (int i = 0; i < numPCFSamples; i++)
	{
		vec2 offset = SamplePoisson(i) * uvRadius;
		float shadowMapDepth = textureLod(shadowMap, vec3((shadowCoords.xy * 0.5 + 0.5) + offset, cascade), 0).r;
		sum += step(shadowCoords.z - bias, shadowMapDepth);
	}
	return sum / numPCFSamples;
}

// Return a value between 0 to 1 that represent how much the fragment is lit (been 1 fully lit and 0 fully in shadowed)
float PCSS_DirectionalLight(sampler2DArray shadowMap, uint cascade, vec3 shadowCoords, float uvLightSize)
{
	// 1. Find the Blocker distance
	float blockerDistance = FindBlockerDistance_DirectionalLight(shadowMap, cascade, shadowCoords, uvLightSize);
	if (blockerDistance == -1) // No occlusion
		return 1.f;
	// 2. Penumbra estimation
	float penumbraWidth = (shadowCoords.z - blockerDistance) * uvLightSize / blockerDistance;
	// 3. PCSS
	float uvRadius = penumbraWidth * NEAR / shadowCoords.z; // Do we need to divide by shadowCoords.z?
	uvRadius = min(uvRadius, 0.002f);
	// 4. Filtering through PCF
	return PCF_DirectionalLight(shadowMap, cascade, shadowCoords, uvRadius) * ShadowFade;
}


float HardShadows_DirectionalLight(sampler2DArray shadowMap, uint cascade, vec3 shadowCoords)
{
	float bias = GetShadowBias();
	float shadowMapDepth = texture(shadowMap, vec3((shadowCoords.xy * 0.5 + 0.5), cascade)).r;
	return step(shadowCoords.z - bias, shadowMapDepth) * ShadowFade;
}

float CalculateShadow(uint cascadeIndex)
{
	float shadowDistance = u_MaxShadowDistance;
	float transitionDistance = u_ShadowFade;
	float distance = length(vs_Input.ViewPosition);
	ShadowFade = distance - (shadowDistance - transitionDistance);
	ShadowFade /= transitionDistance;
	ShadowFade = clamp(1.f - ShadowFade, 0.f, 1.f);
	float shadowAmount = 0.f;

	if (u_CascadeFading)
	{
		float cascadeTransitionFade = u_CascadeTransitionFade;

		float c0 = smoothstep(u_CascadeSplits[0] + cascadeTransitionFade * 0.5f, u_CascadeSplits[0] - cascadeTransitionFade * 0.5f, vs_Input.ViewPosition.z);
		float c1 = smoothstep(u_CascadeSplits[1] + cascadeTransitionFade * 0.5f, u_CascadeSplits[1] - cascadeTransitionFade * 0.5f, vs_Input.ViewPosition.z);
		float c2 = smoothstep(u_CascadeSplits[2] + cascadeTransitionFade * 0.5f, u_CascadeSplits[2] - cascadeTransitionFade * 0.5f, vs_Input.ViewPosition.z);
		if (c0 > 0.f && c0 < 1.f)
		{
			// Sample 0 & 1
			vec3 shadowMapCoords = (vs_Input.ShadowMapCoords[0].xyz / vs_Input.ShadowMapCoords[0].w);
			float shadowAmount0 = u_SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTex, 0, shadowMapCoords, u_LightSize) : HardShadows_DirectionalLight(u_ShadowMapTex, 0, shadowMapCoords);
			shadowMapCoords = (vs_Input.ShadowMapCoords[1].xyz / vs_Input.ShadowMapCoords[1].w);
			float shadowAmount1 = u_SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTex, 1, shadowMapCoords, u_LightSize) : HardShadows_DirectionalLight(u_ShadowMapTex, 1, shadowMapCoords);

			shadowAmount = mix(shadowAmount0, shadowAmount1, c0);
		}
		else if (c1 > 0.f && c1 < 1.f)
		{
			// Sample 1 & 2
			vec3 shadowMapCoords = (vs_Input.ShadowMapCoords[1].xyz / vs_Input.ShadowMapCoords[1].w);
			float shadowAmount1 =  u_SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTex, 1, shadowMapCoords, u_LightSize) : HardShadows_DirectionalLight(u_ShadowMapTex, 1, shadowMapCoords);
			shadowMapCoords = (vs_Input.ShadowMapCoords[2].xyz / vs_Input.ShadowMapCoords[2].w);
			float shadowAmount2 = u_SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTex, 2, shadowMapCoords, u_LightSize) : HardShadows_DirectionalLight(u_ShadowMapTex, 2, shadowMapCoords);

			shadowAmount = mix(shadowAmount1, shadowAmount2, c1);
		}
		else if (c2 > 0.f && c2 < 1.f)
		{
			// Sample 2 & 3
			vec3 shadowMapCoords = (vs_Input.ShadowMapCoords[2].xyz / vs_Input.ShadowMapCoords[2].w);
			float shadowAmount2 = u_SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTex, 2, shadowMapCoords, u_LightSize) : HardShadows_DirectionalLight(u_ShadowMapTex, 2, shadowMapCoords);
			shadowMapCoords = (vs_Input.ShadowMapCoords[3].xyz / vs_Input.ShadowMapCoords[3].w);
			float shadowAmount3 = u_SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTex, 3, shadowMapCoords, u_LightSize) : HardShadows_DirectionalLight(u_ShadowMapTex, 3, shadowMapCoords);

			shadowAmount = mix(shadowAmount2, shadowAmount3, c2);
		}
		else
		{
			vec3 shadowMapCoords = (vs_Input.ShadowMapCoords[cascadeIndex].xyz / vs_Input.ShadowMapCoords[cascadeIndex].w);
			shadowAmount =  u_SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTex, cascadeIndex, shadowMapCoords, u_LightSize) : HardShadows_DirectionalLight(u_ShadowMapTex, cascadeIndex, shadowMapCoords);
		}
	}
	else
	{
		vec3 shadowMapCoords = (vs_Input.ShadowMapCoords[cascadeIndex].xyz / vs_Input.ShadowMapCoords[cascadeIndex].w);
		shadowAmount =  u_SoftShadows ? PCSS_DirectionalLight(u_ShadowMapTex, cascadeIndex, shadowMapCoords, u_LightSize) : HardShadows_DirectionalLight(u_ShadowMapTex, cascadeIndex, shadowMapCoords);
	}

	return shadowAmount;
}

/////////////////////////////////////////////
// PBR
/////////////////////////////////////////////

// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2
// -- Approximates the relative surface area of microfacets 
// -- exactly aligned to the (halfway) vector h
float NdfGGX(float cosLh, float roughness)
{
	float alpha = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

// Single term for separable Schlick-GGX below.
float GaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

// Schlick-GGX approximation of geometric attenuation function using Smith's method.
// -- Approximates the relative surface area where its micro surface-details 
// -- overshadow  each other, causing light rays to be occluded. In other words
// -- What concentration of micro facet transferns lights from the light direction 
// -- to the view (outgoing) direction.
float GaSchlickGGX(float cosLi, float NdotV, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
	return GaSchlickG1(cosLi, k) * GaSchlickG1(NdotV, k);
}

// Shlick's approximation of the Fresnel factor.
// Return RGB value (also between 0 - 1)
// -- Describes the ratio of light that gets reflected over the light that gets refracted
vec3 FresnelSchlick(vec3 F0, float cosTheta)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}
// Shlick's approximation of the Fresnel factor.
// Return RGB value (also between 0 - 1)
// -- Describes the ratio of light that gets reflected over the light that gets refracted
// -- Greater the roughness less the reflectivity of the surface
vec3 FresnelSchlickRoughness(vec3 F0, float cosTheta, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
} 

// Calculate Direct Light
// F0 - Fresnel reflectance, is the interpolation between the albedo and the metallic
vec3 DirectionalLighting(vec3 F0)
{
	vec3 directLighting = vec3(0.0);
	for(int i = 0; i < 1; i++)
	{
		vec3 lightDirection = u_DirectionalLights[i].Direction.xyz;
		vec3 lightRadience = u_DirectionalLights[i].Radience.xyz *  u_DirectionalLights[i].Intensity;

		// Half-vector between lightDirection and view. => Direction in which the microfacet needs to be pointing
		vec3 lightHalfway = normalize(lightDirection + m_Params.View);
		// Calculate angles between surface normal and various light vectors.
		float cosLi = max(0.0, dot(m_Params.Normal, lightDirection));
		float cosLh = max(0.0, dot(m_Params.Normal, lightHalfway));

		// Calculate Fresnel term for direct lighting. 
		// (how reflective the micro facet is)
		vec3 F  = FresnelSchlickRoughness(F0, max(0.0, dot(lightHalfway, m_Params.View)), m_Params.Roughness);
		// Calculate normal distribution for specular BRDF. 
		// (What is the concentration of micro facet with normals aligend with the halfway)
		float D = NdfGGX(cosLh, m_Params.Roughness);
		// Calculate geometric attenuation for specular BRDF. 
		// (if the micro facet is lit)
		float G = GaSchlickGGX(cosLi, m_Params.NdotV, m_Params.Roughness);

		// Cook-Torrance specular microfacet BRDF.
		vec3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * m_Params.NdotV);

		// Diffuse scattering happens due to light being refracted multiple times by a dielectric medium.
		// Metals on the other hand either reflect or absorb energy, so diffuse contribution is always zero.
		// To be energy conserving we must scale diffuse BRDF contribution based on Fresnel factor & metalness.
		vec3 kd = mix(vec3(1.0) - F, vec3(0.0), m_Params.Metalness);

		// Lambert diffuse BRDF.
		// We don't scale by 1/PI for lighting & material units to be more convenient.
		// See: https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
		vec3 diffuseBRDF = kd * m_Params.Albedo;

		// Total contribution for this light.
		directLighting += (diffuseBRDF + specularBRDF) * lightRadience * cosLi;
	}

	return directLighting;
}
// Calculate Image Based Lighting (IBL)
// F0 - Fresnel reflectance, is the interpolation between the albedo and the metallic
vec3 IBL(vec3 F0)
{
	// Diffuse
	// Calculate Fresnel term for ambient lighting.
	// Since we use pre-filtered cubemap(s) and irradiance is coming from many directions
	// use an angle between normal and view (outgoing light) instead of angle with light's half-vector.
	vec3 F = FresnelSchlickRoughness(F0, m_Params.NdotV,  m_Params.Roughness);
	vec3 kD = (1.0 - F) * (1.0 - m_Params.Metalness);
	vec3 irradiance = texture(u_EnvIrradianceTex, m_Params.Normal).rgb;
	// Irradiance map contains exitant radiance assuming Lambertian BRDF, no need to scale by 1/PI here either.
	vec3 diffuse = kD * irradiance * m_Params.Albedo;
	
	// Specular
	int envRadianceTexLevels = textureQueryLevels(u_EnvRadianceTex);
	vec3 reflectV =  2.0 * dot(m_Params.View, m_Params.Normal) * m_Params.Normal - m_Params.View;
	// Get the indirect specular reflections of the surface by sampling the pre-filtered environment map using the reflection vector
	vec3 specularIrradiance = textureLod(u_EnvRadianceTex, reflectV, (m_Params.Roughness) * envRadianceTexLevels).rgb;
	// Sample from the BRDF lookup texture given the material's roughness and the angle between the normal and view vector
	// 1.0 - roughness for y-coord because texture was generated opside down
	vec2 specularBRDF = texture(u_BRDFLUTTex, vec2(m_Params.NdotV, 1.0 - m_Params.Roughness)).rg;
	// Total specular IBL contribution
	vec3 specular = specularIrradiance * (F0 * specularBRDF.x + specularBRDF.y);

	return diffuse + specular;
}

void main() 
{
	m_Params.Albedo =  texture(u_AlbedoTex, vs_Input.TexCoord).rgb * u_MaterialUniforms.AlbedoColor;
	float alpha = texture(u_AlbedoTex, vs_Input.TexCoord).a;
	m_Params.Metalness = texture(u_MetallicTex, vs_Input.TexCoord).r * u_MaterialUniforms.Metalness;
	m_Params.Roughness = texture(u_RoughnessTex, vs_Input.TexCoord).r * u_MaterialUniforms.Roughness;
	// Minimum roughness of 0.05 to keep specular highlight
	m_Params.Roughness = max(m_Params.Roughness, 0.05f);
	// Normals
	if(u_MaterialUniforms.UseNormalMap)
	{
		// Normal Map has the value between 0 to 1 (because is basically a color) but we to re-map to -1 to 1
		m_Params.Normal = normalize(texture(u_NormalTex, vs_Input.TexCoord).rgb * 2 - 1);
		// Convert the normals from local TangentSpace to WorldSpace
		m_Params.Normal = normalize(vs_Input.WorldNormals * m_Params.Normal);
	}
	else
	{
		m_Params.Normal = normalize(vs_Input.Normal);
	}
	// Outgoing light direction (vector from world-space fragment position to the "eye").
    m_Params.View = normalize(u_CameraPosition - vs_Input.WorldPosition);
	// Angle between surface normal and outgoing light direction.
	m_Params.NdotV = max(0.f, dot(m_Params.Normal, m_Params.View));
	// Get shadow cascade
	uint cascadeIndex = 0;
	const uint SHADOW_MAP_CASCADE_COUNT = 4;
	for (uint i = 0; i < SHADOW_MAP_CASCADE_COUNT - 1; i++)
	{
		if (vs_Input.ViewPosition.z < u_CascadeSplits[i])
			cascadeIndex = i + 1;
	}
	// Calculate the shadow amount of the fragment
	float shadowAmount = CalculateShadow(cascadeIndex);
	// Fresnel reflectance at normal incidence (for metals use albedo color).
	vec3 F0 = mix(Fdielectric, m_Params.Albedo,  m_Params.Metalness);
	// Direct lighting calculation for analytical lights.
	vec3 directLighting = DirectionalLighting(F0) * shadowAmount;
	vec3 ibl = IBL(F0);
	vec3 color = directLighting + ibl;

	if (u_ShowCascades)
	{
		switch (cascadeIndex)
		{
		case 0:
			color.rgb *= vec3(1.f, 0.25f, 0.25f);
			break;
		case 1:
			color.rgb *= vec3(0.25f, 1.f, 0.25f);
			break;
		case 2:
			color.rgb *= vec3(0.25f, 0.25f, 1.f);
			break;
		case 3:
			color.rgb *= vec3(1.f, 1.f, 0.25f);
			break;
		}
	}
	o_FragmentColor = vec4(color, 1.f);
}