#version 450
#extension GL_EXT_nonuniform_qualifier : require

const float PI = 3.14159265358979323846;
const uint MaxMaterialTextures = 128u;

struct GPUMaterialData
{
    vec4 AlbedoColor;
    // x = roughness, y = metalness, z = emission, w = use normal map.
    vec4 Parameters;
    // x = albedo, y = normal, z = metalness, w = roughness.
    uvec4 TextureIndices;
};

struct DirectionalLight
{
    // xyz = direction from the surface toward the light, w = intensity.
    vec4 DirectionAndIntensity;
    vec4 Radiance;
};

layout(std140, set = 0, binding = 0) uniform CameraData
{
    mat4 ViewProjection;
    mat4 InverseViewProjection;
    vec4 CameraPosition;
    vec4 Presentation;
} Camera;

layout(std430, set = 0, binding = 1) readonly buffer MaterialTable
{
    GPUMaterialData Materials[];
} MaterialBuffer;

layout(set = 0, binding = 2) uniform sampler2D MaterialTextures[128];
layout(set = 0, binding = 3) uniform samplerCube EnvironmentRadiance;
layout(set = 0, binding = 4) uniform samplerCube EnvironmentIrradiance;

layout(std140, set = 0, binding = 5) uniform ForwardLightingData
{
    // x = SkyLight intensity, y = radiance display LOD.
    vec4 SkyLightParameters;
    // x = active directional-light count.
    uvec4 DirectionalLightParameters;
    DirectionalLight DirectionalLights[4];
} Lighting;

// Cascaded shadow map for DirectionalLights[0] only (the primary/first light).
layout(std140, set = 0, binding = 6) uniform ShadowData
{
    mat4 CascadeViewProjection[4];
    // One component per cascade; distance-from-camera thresholds.
    vec4 CascadeSplitDepths;
} Shadow;
layout(set = 0, binding = 7) uniform sampler2D ShadowMaps[4];

layout(location = 0) in vec3 v_WorldPosition;
layout(location = 1) in vec3 v_WorldNormal;
layout(location = 2) in vec3 v_WorldTangent;
layout(location = 3) in vec3 v_WorldBinormal;
layout(location = 4) in vec2 v_TexCoord;
layout(location = 5) flat in uint v_MaterialIndex;

layout(location = 0) out vec4 o_Color;

bool IsValidTextureIndex(uint textureIndex)
{
    return textureIndex < MaxMaterialTextures;
}

vec4 SampleMaterialTexture(uint textureIndex, vec2 texCoord)
{
    // Mesh UVs stay canonical. stb_image source rows use the opposite V origin.
    vec2 samplingUV = vec2(texCoord.x, 1.0 - texCoord.y);
    return texture(MaterialTextures[nonuniformEXT(textureIndex)], samplingUV);
}

vec3 FresnelSchlick(float cosine, vec3 reflectanceAtNormalIncidence)
{
    return reflectanceAtNormalIncidence +
        (vec3(1.0) - reflectanceAtNormalIncidence) * pow(1.0 - clamp(cosine, 0.0, 1.0), 5.0);
}

float GGXDistribution(float normalDotHalf, float alpha)
{
    float alphaSquared = alpha * alpha;
    float denominator = normalDotHalf * normalDotHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(PI * denominator * denominator, 1.0e-6);
}

float SmithGGXMasking(float normalDotDirection, float alpha)
{
    float alphaSquared = alpha * alpha;
    float directionSquared = normalDotDirection * normalDotDirection;
    float denominator = normalDotDirection + sqrt(max(
        alphaSquared + (1.0 - alphaSquared) * directionSquared,
        0.0));
    return (2.0 * normalDotDirection) / max(denominator, 1.0e-6);
}

vec3 EvaluatePBR(
    vec3 viewDirection,
    vec3 lightDirection,
    vec3 normal,
    vec3 albedo,
    float roughness,
    float metalness)
{
    float normalDotView = max(dot(normal, viewDirection), 0.0);
    float normalDotLight = max(dot(normal, lightDirection), 0.0);
    if (normalDotView <= 0.0 || normalDotLight <= 0.0)
        return vec3(0.0);

    vec3 halfVector = normalize(viewDirection + lightDirection);
    float normalDotHalf = max(dot(normal, halfVector), 0.0);
    float viewDotHalf = max(dot(viewDirection, halfVector), 0.0);
    float alpha = roughness * roughness;

    float distribution = GGXDistribution(normalDotHalf, alpha);
    float geometry =
        SmithGGXMasking(normalDotView, alpha) *
        SmithGGXMasking(normalDotLight, alpha);
    vec3 reflectanceAtNormalIncidence = mix(vec3(0.04), albedo, metalness);
    vec3 fresnel = FresnelSchlick(viewDotHalf, reflectanceAtNormalIncidence);

    vec3 specular = distribution * geometry * fresnel /
        max(4.0 * normalDotView * normalDotLight, 1.0e-6);
    vec3 diffuse = (vec3(1.0) - fresnel) *
        (1.0 - metalness) * albedo / PI;
    return diffuse + specular;
}

// 16-tap Poisson disk, subset of a larger distribution used for percentage-
// closer filtering. Fixed filter radius rather than full PCSS blocker-search:
// PCSS's penumbra-scaling formula assumes a perspective light projection, and
// directional lights here are orthographic, so blocker-search distance doesn't
// change the penumbra size the way it would for a spot/point light.
const vec2 PoissonDisk[16] = vec2[](
    vec2(-0.884081, 0.124488), vec2(-0.714377, 0.027940), vec2(-0.747945, 0.227922),
    vec2(-0.939609, 0.243634), vec2(-0.985465, 0.045534), vec2(-0.861367, -0.136222),
    vec2(-0.466938, 0.014526), vec2(-0.558207, 0.212662), vec2(-0.578447, -0.095822),
    vec2(-0.319776, -0.312166), vec2(-0.243704, -0.186378), vec2(-0.100006, -0.229116),
    vec2(0.071650, -0.384284), vec2(0.163314, -0.217231), vec2(0.132993, 0.085170),
    vec2(0.183621, -0.713242)
);

float SampleShadowCascade(uint cascadeIndex, vec2 uv, float compareDepth, float bias)
{
    vec2 texelSize = 1.0 / vec2(textureSize(ShadowMaps[nonuniformEXT(cascadeIndex)], 0));
    float lit = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        vec2 offset = PoissonDisk[i] * texelSize * 2.0;
        float closestDepth = texture(ShadowMaps[nonuniformEXT(cascadeIndex)], uv + offset).r;
        lit += (compareDepth - bias) <= closestDepth ? 1.0 : 0.0;
    }
    return lit / 16.0;
}

// Percentage-closer-filtered shadow lookup for DirectionalLights[0]. Cascade
// selection uses distance from the camera rather than exact view-space Z, so
// the fragment shader doesn't need a separate view matrix.
float EvaluateShadow(vec3 worldPosition, vec3 normal, vec3 lightDirection, float distanceFromCamera)
{
    uint cascadeIndex = 3u;
    for (uint i = 0u; i < 3u; ++i)
    {
        if (distanceFromCamera < Shadow.CascadeSplitDepths[i])
        {
            cascadeIndex = i;
            break;
        }
    }

    vec4 shadowClip = Shadow.CascadeViewProjection[cascadeIndex] * vec4(worldPosition, 1.0);
    vec3 shadowCoords = shadowClip.xyz / shadowClip.w;
    if (shadowCoords.z < 0.0 || shadowCoords.z > 1.0)
        return 1.0;

    vec2 shadowUV = shadowCoords.xy * 0.5 + 0.5;
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 1.0;

    // Slope-scaled bias: grows as the surface tilts away from the light, to
    // avoid self-shadowing (shadow acne) without over-biasing head-on surfaces.
    // Each cascade's ortho depth range spans tens to 100+ world units
    // (2*radius + 15), so a bias this small in normalized [0,1] depth space
    // corresponds to a fraction of a world unit -- too little to avoid acne.
    const float minimumBias = 0.006;
    float bias = max(minimumBias * (1.0 - dot(normal, lightDirection)), minimumBias * 0.25);
    return SampleShadowCascade(cascadeIndex, shadowUV, shadowCoords.z, bias);
}

void main()
{
    GPUMaterialData material = MaterialBuffer.Materials[v_MaterialIndex];
    vec3 albedo = material.AlbedoColor.rgb;
    if (IsValidTextureIndex(material.TextureIndices.x))
        albedo *= SampleMaterialTexture(material.TextureIndices.x, v_TexCoord).rgb;

    float roughness = clamp(material.Parameters.x, 0.045, 1.0);
    if (IsValidTextureIndex(material.TextureIndices.w))
        roughness *= SampleMaterialTexture(material.TextureIndices.w, v_TexCoord).r;
    roughness = clamp(roughness, 0.045, 1.0);

    float metalness = clamp(material.Parameters.y, 0.0, 1.0);
    if (IsValidTextureIndex(material.TextureIndices.z))
        metalness *= SampleMaterialTexture(material.TextureIndices.z, v_TexCoord).r;
    metalness = clamp(metalness, 0.0, 1.0);

    vec3 normal = normalize(v_WorldNormal);
    if (material.Parameters.w > 0.5 && IsValidTextureIndex(material.TextureIndices.y))
    {
        vec3 tangent = normalize(v_WorldTangent - normal * dot(v_WorldTangent, normal));
        vec3 referenceBinormal = normalize(v_WorldBinormal);
        float tangentSign = dot(cross(normal, tangent), referenceBinormal) < 0.0 ? -1.0 : 1.0;
        vec3 binormal = tangentSign * normalize(cross(normal, tangent));
        vec3 tangentNormal = SampleMaterialTexture(material.TextureIndices.y, v_TexCoord).xyz * 2.0 - 1.0;
        normal = normalize(mat3(tangent, binormal, normal) * tangentNormal);
    }

    vec3 viewDirection = normalize(Camera.CameraPosition.xyz - v_WorldPosition);
    vec3 radiance = vec3(0.0);
    uint lightCount = min(Lighting.DirectionalLightParameters.x, 4u);
    for (uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex)
    {
        DirectionalLight light = Lighting.DirectionalLights[lightIndex];
        vec3 lightDirection = normalize(light.DirectionAndIntensity.xyz);
        float normalDotLight = max(dot(normal, lightDirection), 0.0);

        // Only DirectionalLights[0] casts shadows for now (see ShadowPass.h).
        float shadowFactor = 1.0;
        if (lightIndex == 0u)
        {
            float distanceFromCamera = length(v_WorldPosition - Camera.CameraPosition.xyz);
            shadowFactor = EvaluateShadow(v_WorldPosition, normal, lightDirection, distanceFromCamera);
        }

        radiance += EvaluatePBR(
            viewDirection,
            lightDirection,
            normal,
            albedo,
            roughness,
            metalness) *
            normalDotLight * light.Radiance.rgb * light.DirectionAndIntensity.w * shadowFactor;
    }

    float skyIntensity = Lighting.SkyLightParameters.x;
    vec3 reflectanceAtNormalIncidence = mix(vec3(0.04), albedo, metalness);
    float normalDotView = max(dot(normal, viewDirection), 0.0);
    vec3 fresnel = FresnelSchlick(normalDotView, reflectanceAtNormalIncidence);
    vec3 diffuseIBL = texture(EnvironmentIrradiance, normal).rgb *
        albedo * (1.0 - metalness) * (vec3(1.0) - fresnel);
    vec3 reflectionDirection = reflect(-viewDirection, normal);
    float radianceMipCount = float(textureQueryLevels(EnvironmentRadiance));
    vec3 specularIBL = textureLod(
        EnvironmentRadiance,
        reflectionDirection,
        roughness * max(radianceMipCount - 1.0, 0.0)).rgb * fresnel;
    radiance += (diffuseIBL + specularIBL) * skyIntensity;
    radiance += albedo * max(material.Parameters.z, 0.0);

    // SceneColor stays linear HDR; tonemap/exposure/gamma happen once in the
    // Composite pass instead of being duplicated in every shading shader.
    o_Color = vec4(radiance, 1.0);
}
