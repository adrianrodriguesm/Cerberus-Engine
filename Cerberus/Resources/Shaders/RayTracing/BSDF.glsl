#ifndef CERBERUS_RT_BSDF_GLSL
#define CERBERUS_RT_BSDF_GLSL

const float CERBERUS_PI = 3.14159265358979323846;
const float CERBERUS_INV_PI = 1.0 / CERBERUS_PI;
const float CERBERUS_BSDF_EPSILON = 1e-6;

struct BSDFSample
{
    vec3 Direction;
    vec3 Weight;
    // Which lobe produced Direction. The denoiser split routes the whole
    // indirect contribution of a primary path into the diffuse or the specular
    // signal based on the primary hit's value of this.
    bool IsSpecular;
};

uint RandomUint(inout uint state)
{
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat(inout uint state)
{
    return float(RandomUint(state)) * (1.0 / 4294967296.0);
}

float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void BuildOrthonormalBasis(vec3 normal, out vec3 tangent, out vec3 binormal)
{
    vec3 axis = abs(normal.z) < 0.999
        ? vec3(0.0, 0.0, 1.0)
        : vec3(1.0, 0.0, 0.0);

    tangent = normalize(cross(axis, normal));
    binormal = cross(normal, tangent);
}

vec3 TangentToWorld(vec3 direction, vec3 normal)
{
    vec3 tangent;
    vec3 binormal;
    BuildOrthonormalBasis(normal, tangent, binormal);
    return normalize(
        tangent * direction.x +
        binormal * direction.y +
        normal * direction.z);
}

vec3 SampleCosineHemisphere(vec2 randomSample, vec3 normal)
{
    float radius = sqrt(randomSample.x);
    float phi = 2.0 * CERBERUS_PI * randomSample.y;
    vec3 localDirection = vec3(
        radius * cos(phi),
        radius * sin(phi),
        sqrt(max(0.0, 1.0 - randomSample.x)));
    return TangentToWorld(localDirection, normal);
}

float GGXDistribution(float normalDotHalf, float alpha)
{
    float alphaSquared = alpha * alpha;
    float denominator = normalDotHalf * normalDotHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(CERBERUS_PI * denominator * denominator, CERBERUS_BSDF_EPSILON);
}

float SmithGGXMasking(float normalDotDirection, float alpha)
{
    float alphaSquared = alpha * alpha;
    float directionSquared = normalDotDirection * normalDotDirection;
    float denominator = normalDotDirection + sqrt(
        max(alphaSquared + (1.0 - alphaSquared) * directionSquared, 0.0));
    return (2.0 * normalDotDirection) / max(denominator, CERBERUS_BSDF_EPSILON);
}

vec3 FresnelSchlick(float directionDotHalf, vec3 reflectanceAtNormalIncidence)
{
    float fresnel = pow(1.0 - clamp(directionDotHalf, 0.0, 1.0), 5.0);
    return reflectanceAtNormalIncidence +
        (vec3(1.0) - reflectanceAtNormalIncidence) * fresnel;
}

// Analytic preintegrated environment BRDF (Karis 2013 split-sum, Lazarov's
// mobile fit). Returns the RGB factor that a perfectly denoised specular
// *illumination* signal must be multiplied by to reconstruct specular radiance:
//   specularRadiance ~= specularIllumination * EnvBRDFApprox(F0, roughness, NdotV)
// This is the specular analogue of the diffuse albedo used for demodulation, so
// the denoiser can filter a signal that is free of the BRDF's angular/Fresnel
// variation. No LUT needed.
vec3 EnvBRDFApprox(vec3 reflectanceAtNormalIncidence, float roughness, float normalDotView)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * max(normalDotView, 0.0))) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return reflectanceAtNormalIncidence * ab.x + vec3(ab.y);
}

vec3 SampleGGXHalfVector(vec2 randomSample, vec3 normal, float alpha)
{
    float alphaSquared = alpha * alpha;
    float phi = 2.0 * CERBERUS_PI * randomSample.x;
    float cosTheta = sqrt(
        (1.0 - randomSample.y) /
        max(1.0 + (alphaSquared - 1.0) * randomSample.y, CERBERUS_BSDF_EPSILON));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    return TangentToWorld(
        vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta),
        normal);
}

// Evaluates the BRDF for an explicitly chosen light direction, returning the
// diffuse and specular terms separately (the caller applies the cosine). Used by
// next-event estimation: the combined form for the path radiance, the split
// terms for the denoiser's diffuse/specular demodulation. Returns f(view, light).
void EvaluatePBRBSDFSeparate(
    vec3 viewDirection,
    vec3 lightDirection,
    vec3 normal,
    vec3 albedo,
    float roughness,
    float metalness,
    out vec3 diffuseBRDF,
    out vec3 specularBRDF)
{
    diffuseBRDF = vec3(0.0);
    specularBRDF = vec3(0.0);

    albedo = max(albedo, vec3(0.0));
    roughness = clamp(roughness, 0.045, 1.0);
    metalness = clamp(metalness, 0.0, 1.0);

    float normalDotView = max(dot(normal, viewDirection), 0.0);
    float normalDotLight = max(dot(normal, lightDirection), 0.0);
    if (normalDotView <= CERBERUS_BSDF_EPSILON ||
        normalDotLight <= CERBERUS_BSDF_EPSILON)
    {
        return;
    }

    vec3 halfVectorSum = viewDirection + lightDirection;
    float halfVectorLengthSquared = dot(halfVectorSum, halfVectorSum);
    if (halfVectorLengthSquared <= CERBERUS_BSDF_EPSILON)
        return;

    vec3 halfVector = halfVectorSum * inversesqrt(halfVectorLengthSquared);
    float normalDotHalf = max(dot(normal, halfVector), 0.0);
    float viewDotHalf = max(dot(viewDirection, halfVector), 0.0);

    float alpha = roughness * roughness;
    float distribution = GGXDistribution(normalDotHalf, alpha);
    float geometry =
        SmithGGXMasking(normalDotView, alpha) *
        SmithGGXMasking(normalDotLight, alpha);

    vec3 reflectanceAtNormalIncidence = mix(vec3(0.04), albedo, metalness);
    vec3 fresnel = FresnelSchlick(viewDotHalf, reflectanceAtNormalIncidence);

    vec3 spec =
        distribution * geometry * fresnel /
        max(4.0 * normalDotView * normalDotLight, CERBERUS_BSDF_EPSILON);

    vec3 diff =
        (vec3(1.0) - fresnel) *
        (1.0 - metalness) *
        albedo * CERBERUS_INV_PI;

    bool bad = any(isnan(spec)) || any(isinf(spec)) || any(isnan(diff)) || any(isinf(diff));
    if (bad)
        return;

    diffuseBRDF = diff;
    specularBRDF = spec;
}

vec3 EvaluatePBRBSDF(
    vec3 viewDirection,
    vec3 lightDirection,
    vec3 normal,
    vec3 albedo,
    float roughness,
    float metalness)
{
    vec3 diffuseBRDF;
    vec3 specularBRDF;
    EvaluatePBRBSDFSeparate(viewDirection, lightDirection, normal, albedo,
        roughness, metalness, diffuseBRDF, specularBRDF);
    return diffuseBRDF + specularBRDF;
}

bool SamplePBRBSDF(
    vec3 viewDirection,
    vec3 normal,
    vec3 albedo,
    float roughness,
    float metalness,
    inout uint randomState,
    out BSDFSample bsdfResult)
{
    albedo = max(albedo, vec3(0.0));
    roughness = clamp(roughness, 0.045, 1.0);
    metalness = clamp(metalness, 0.0, 1.0);

    float normalDotView = max(dot(normal, viewDirection), 0.0);
    if (normalDotView <= CERBERUS_BSDF_EPSILON)
        return false;

    vec3 reflectanceAtNormalIncidence = mix(vec3(0.04), albedo, metalness);
    float specularProbability = metalness >= 0.999
        ? 1.0
        : clamp(max(Luminance(reflectanceAtNormalIncidence), 0.15), 0.05, 0.95);

    vec2 randomSample = vec2(RandomFloat(randomState), RandomFloat(randomState));
    if (RandomFloat(randomState) < specularProbability)
    {
        float alpha = roughness * roughness;
        vec3 halfVector = SampleGGXHalfVector(randomSample, normal, alpha);
        float viewDotHalf = dot(viewDirection, halfVector);
        if (viewDotHalf <= CERBERUS_BSDF_EPSILON)
            return false;

        bsdfResult.Direction = normalize(reflect(-viewDirection, halfVector));
        bsdfResult.IsSpecular = true;
    }
    else
    {
        bsdfResult.Direction = SampleCosineHemisphere(randomSample, normal);
        bsdfResult.IsSpecular = false;
    }

    float normalDotLight = max(dot(normal, bsdfResult.Direction), 0.0);
    if (normalDotLight <= CERBERUS_BSDF_EPSILON)
        return false;

    vec3 halfVectorSum = viewDirection + bsdfResult.Direction;
    float halfVectorLengthSquared = dot(halfVectorSum, halfVectorSum);
    if (halfVectorLengthSquared <= CERBERUS_BSDF_EPSILON)
        return false;

    vec3 halfVector = halfVectorSum * inversesqrt(halfVectorLengthSquared);
    float normalDotHalf = max(dot(normal, halfVector), 0.0);
    float viewDotHalf = max(dot(viewDirection, halfVector), 0.0);

    float alpha = roughness * roughness;
    float distribution = GGXDistribution(normalDotHalf, alpha);
    float geometry =
        SmithGGXMasking(normalDotView, alpha) *
        SmithGGXMasking(normalDotLight, alpha);
    vec3 fresnel = FresnelSchlick(viewDotHalf, reflectanceAtNormalIncidence);

    vec3 specularBRDF =
        distribution * geometry * fresnel /
        max(4.0 * normalDotView * normalDotLight, CERBERUS_BSDF_EPSILON);

    vec3 diffuseBRDF =
        (vec3(1.0) - fresnel) *
        (1.0 - metalness) *
        albedo * CERBERUS_INV_PI;

    float diffusePdf = normalDotLight * CERBERUS_INV_PI;
    float specularPdf =
        distribution * normalDotHalf /
        max(4.0 * viewDotHalf, CERBERUS_BSDF_EPSILON);
    float pdf = mix(diffusePdf, specularPdf, specularProbability);
    if (pdf <= CERBERUS_BSDF_EPSILON)
        return false;

    bsdfResult.Weight = (diffuseBRDF + specularBRDF) * normalDotLight / pdf;
    return !any(isnan(bsdfResult.Weight)) && !any(isinf(bsdfResult.Weight));
}

#endif // CERBERUS_RT_BSDF_GLSL
