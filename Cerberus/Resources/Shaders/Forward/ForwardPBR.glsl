#extension GL_EXT_nonuniform_qualifier : require

const float PI = 3.14159265358979323846;
#include "../Common/RasterMaterial.glsl"

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
    // Splits are ordered; count the boundaries crossed, including equality.
    uvec3 crossedSplits = uvec3(greaterThanEqual(vec3(distanceFromCamera), Shadow.CascadeSplitDepths.xyz));
    uint cascadeIndex = crossedSplits.x + crossedSplits.y + crossedSplits.z;

    vec4 shadowClip = Shadow.CascadeViewProjection[cascadeIndex] * vec4(worldPosition, 1.0);
    vec3 shadowCoords = shadowClip.xyz / shadowClip.w;
    vec2 shadowUV = shadowCoords.xy * 0.5 + 0.5;
    vec3 shadowUVDepth = vec3(shadowUV, shadowCoords.z);
    if (any(lessThan(shadowUVDepth, vec3(0.0))) ||
        any(greaterThan(shadowUVDepth, vec3(1.0))))
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

vec2 SampleRoughnessMetalness(GPUMaterialData material, vec2 uv)
{
    bool packedSpecularGB = material.Flags.x > 0.5;
    // Neutral samples preserve scalar-only materials and independently missing maps.
    vec4 roughnessSample = vec4(1.0);
    if (IsValidTextureIndex(material.TextureIndices.w))
        roughnessSample = SampleMaterialTexture(material.TextureIndices.w, uv);

    vec4 metalnessSample = vec4(1.0);
    if (packedSpecularGB && material.TextureIndices.z == material.TextureIndices.w)
        metalnessSample = roughnessSample;
    else if (IsValidTextureIndex(material.TextureIndices.z))
        metalnessSample = SampleMaterialTexture(material.TextureIndices.z, uv);

    vec2 factors = packedSpecularGB
        ? vec2(roughnessSample.g, metalnessSample.b)
        : vec2(roughnessSample.r, metalnessSample.r);
    // Preserve clamping before and after applying the texture factors.
    vec2 scalar = clamp(material.Parameters.xy, vec2(0.045, 0.0), vec2(1.0));
    return clamp(scalar * factors, vec2(0.045, 0.0), vec2(1.0));
}

vec3 EvaluateMaterialNormal(GPUMaterialData material, vec2 uv)
{
    vec3 normal = normalize(v_WorldNormal);
    if (material.Parameters.w > 0.5 && IsValidTextureIndex(material.TextureIndices.y))
    {
        vec3 tangent = normalize(v_WorldTangent - normal * dot(v_WorldTangent, normal));
        vec3 referenceBinormal = normalize(v_WorldBinormal);
        float tangentSign = dot(cross(normal, tangent), referenceBinormal) < 0.0 ? -1.0 : 1.0;
        vec3 binormal = tangentSign * normalize(cross(normal, tangent));
        vec4 normalSample = SampleMaterialTexture(material.TextureIndices.y, uv);
        vec2 tangentNormalXY = normalSample.xy * 2.0 - 1.0;
        float tangentNormalZ = material.Flags.y > 0.5
            ? sqrt(clamp(1.0 - dot(tangentNormalXY, tangentNormalXY), 0.0, 1.0))
            : normalSample.z * 2.0 - 1.0;
        vec3 tangentNormal = vec3(tangentNormalXY, tangentNormalZ);
        normal = normalize(mat3(tangent, binormal, normal) * tangentNormal);
    }

    return normal * ((material.AlphaParameters.z > 0.5 && !gl_FrontFacing) ? -1.0 : 1.0);
}

struct PBRSurface
{
    vec3 Albedo;
    vec3 Normal;
    float Roughness;
    float Metalness;
};

vec3 EvaluateDirectionalLight(DirectionalLight light, vec3 viewDirection, PBRSurface surface, float shadowFactor)
{
    vec3 lightDirection = normalize(light.DirectionAndIntensity.xyz);
    float normalDotLight = max(dot(surface.Normal, lightDirection), 0.0);
    return EvaluatePBR(viewDirection, lightDirection, surface.Normal, surface.Albedo,
        surface.Roughness, surface.Metalness) * normalDotLight *
        light.Radiance.rgb * light.DirectionAndIntensity.w * shadowFactor;
}

vec3 EvaluateDirectionalLighting(vec3 viewDirection, PBRSurface surface)
{
    vec3 radiance = vec3(0.0);
    uint lightCount = min(Lighting.DirectionalLightParameters.x, 4u);
    // Only the primary light casts shadows. Resolve its visibility once before
    // the loop; subsequent lights use full visibility without an index branch.
    float shadowFactor = 1.0;
    if (lightCount > 0u)
    {
        DirectionalLight primaryLight = Lighting.DirectionalLights[0];
        vec3 lightDirection = normalize(primaryLight.DirectionAndIntensity.xyz);
        float distanceFromCamera = length(v_WorldPosition - Camera.CameraPosition.xyz);
        shadowFactor = EvaluateShadow(v_WorldPosition, surface.Normal, lightDirection, distanceFromCamera);
    }
    for (uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex)
    {
        radiance += EvaluateDirectionalLight(Lighting.DirectionalLights[lightIndex], viewDirection, surface, shadowFactor);
        shadowFactor = 1.0;
    }
    return radiance;
}

vec3 EvaluateImageBasedLighting(vec3 viewDirection, PBRSurface surface)
{
    float skyIntensity = Lighting.SkyLightParameters.x;
    vec3 reflectanceAtNormalIncidence = mix(vec3(0.04), surface.Albedo, surface.Metalness);
    float normalDotView = max(dot(surface.Normal, viewDirection), 0.0);
    vec3 fresnel = FresnelSchlick(normalDotView, reflectanceAtNormalIncidence);
    vec3 diffuseIBL = texture(EnvironmentIrradiance, surface.Normal).rgb *
        surface.Albedo * (1.0 - surface.Metalness) * (vec3(1.0) - fresnel);
    vec3 reflectionDirection = reflect(-viewDirection, surface.Normal);
    float radianceMipCount = float(textureQueryLevels(EnvironmentRadiance));
    vec3 specularIBL = textureLod(
        EnvironmentRadiance,
        reflectionDirection,
        surface.Roughness * max(radianceMipCount - 1.0, 0.0)).rgb * fresnel;
    return (diffuseIBL + specularIBL) * skyIntensity;
}

vec3 EvaluateMaterialEmission(GPUMaterialData material, vec2 uv, vec3 albedo)
{
    vec3 emissiveColor = albedo;
    if (IsValidTextureIndex(material.TextureIndices2.x))
        emissiveColor = SampleMaterialTexture(material.TextureIndices2.x, uv).rgb;
    return emissiveColor * max(material.Parameters.z, 0.0);
}

void main()
{
    GPUMaterialData material = MaterialBuffer.Materials[v_MaterialIndex];
    vec4 baseColor = SampleMaterialAlbedo(material, v_TexCoord);
#ifdef CERBERUS_MASKED
    AlphaTest(material, v_TexCoord, baseColor.a);
#endif
    vec2 roughnessMetalness = SampleRoughnessMetalness(material, v_TexCoord);
    PBRSurface surface;
    surface.Albedo = baseColor.rgb;
    surface.Normal = EvaluateMaterialNormal(material, v_TexCoord);
    surface.Roughness = roughnessMetalness.x;
    surface.Metalness = roughnessMetalness.y;

    vec3 viewDirection = normalize(Camera.CameraPosition.xyz - v_WorldPosition);
    vec3 radiance = EvaluateDirectionalLighting(viewDirection, surface);
    radiance += EvaluateImageBasedLighting(viewDirection, surface);
    radiance += EvaluateMaterialEmission(material, v_TexCoord, surface.Albedo);

    // SceneColor stays linear HDR; tonemap/exposure/gamma happen once in the
    // Composite pass instead of being duplicated in every shading shader.
    o_Color = vec4(radiance, 1.0);
}
