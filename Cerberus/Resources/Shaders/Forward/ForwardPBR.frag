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

vec3 ACESFilm(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
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
        radiance += EvaluatePBR(
            viewDirection,
            lightDirection,
            normal,
            albedo,
            roughness,
            metalness) *
            normalDotLight * light.Radiance.rgb * light.DirectionAndIntensity.w;
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

    o_Color = vec4(ACESFilm(radiance * max(Camera.Presentation.x, 0.0)), 1.0);
}
