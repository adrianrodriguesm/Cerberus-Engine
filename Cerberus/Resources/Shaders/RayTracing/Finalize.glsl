#type compute
#version 450 core

// Turns a linear-HDR image into the display-ready buffer the editor samples.
// In reference mode the source is the progressive mean (ReferenceAccumulate);
// once the denoiser lands it is the filtered SVGF output instead. Same tone map
// the raygen used to apply inline before CL-B.
//
// DebugView != 0 replaces the lit output with a G-buffer visualisation so the
// denoiser inputs (motion vectors, normals, depth, albedo, mesh id) can be
// eyeballed. Dynamically uniform, so no divergence cost when it is 0.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, rgba32f) readonly  uniform image2D u_Source;
layout(binding = 1, rgba32f) writeonly uniform image2D u_Output;
layout(binding = 2, rg16f)   readonly  uniform image2D u_MotionVector;
layout(binding = 3, rgba32f) readonly  uniform image2D u_NormalDepth;
layout(binding = 4, rgba16f) readonly  uniform image2D u_Albedo;
layout(binding = 5, rgba32f) readonly  uniform image2D u_PositionMeshID;
layout(binding = 6, rg16f)   readonly  uniform image2D u_Material;     // .x roughness, .y metalness
// Diffuse/specular split: demodulated illumination + raw emissive, and the
// specular demod factor (envBRDF) the raygen used. Recombine() below reverses
// the demodulation to build the lit image in denoiser mode.
layout(binding = 7,  rgba32f) readonly uniform image2D u_RadianceDiffuse;
layout(binding = 8,  rgba32f) readonly uniform image2D u_RadianceSpecular;
layout(binding = 9,  rgba32f) readonly uniform image2D u_RadianceEmissive;
layout(binding = 10, rgba16f) readonly uniform image2D u_SpecularAlbedo;  // envBRDF, clamped
// Reprojected specular illumination EMA out of SVGFTemporalSplit. Feeds the
// "spec EMA raw" diagnostic view. (Binding 11 retired with its debug views.)
layout(binding = 12, rgba16f) readonly uniform image2D u_SplitTemporalSpecular;
// Final split a-trous output per signal. The lit path (Recombine) consumes both.
layout(binding = 13, rgba16f) readonly uniform image2D u_SplitAtrousDiffuse;
layout(binding = 14, rgba16f) readonly uniform image2D u_SplitAtrousSpecular;
// Specular temporal moments, both ping-pong slots (.z = history length,
// .w = applied blend alpha). Diagnostic debug view only.
layout(binding = 15, rgba32f) readonly uniform image2D u_SplitMomentsSpec0;
layout(binding = 16, rgba32f) readonly uniform image2D u_SplitMomentsSpec1;

layout(push_constant) uniform Push
{
    float Exposure;
    // Index into kDebugViews: 0 lit, 1 motion, 2 normal, 3 linear depth,
    // 4 albedo, 5 mesh id, 6 variance, 7 material, 8 split recombined (raw),
    // 9 spec alpha/history, 10 spec EMA raw.
    uint  DebugView;
    // 1 = denoiser mode: build the lit image by recombining the filtered split
    // a-trous outputs (diffuse * diffuseAlbedo + specular * envBRDF + emissive).
    // 0 = reference mode: u_Source already holds the lit progressive mean.
    uint  Remodulate;
    // m_DenoiserFrameIndex & 1: which ping-pong slot the split temporal pass
    // wrote this frame. For the specular alpha/history diagnostic debug view.
    uint  SplitParity;
} u_Push;

// Must match SVGFTemporalSplit.glsl: demodulate and re-modulate with the same
// clamped albedo so the round trip is exact.
const float CERBERUS_ALBEDO_FLOOR = 0.05;

vec3 ACESToneMap(vec3 color)
{
    mat3 inputTransform = mat3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777);

    mat3 outputTransform = mat3(
        1.60475, -0.10208, -0.00327,
       -0.53108,  1.10813, -0.07276,
       -0.07367, -0.00605,  1.07602);

    vec3 transformed = inputTransform * max(color, vec3(0.0));
    vec3 numerator = transformed * (transformed + 0.0245786) - 0.000090537;
    vec3 denominator = transformed * (0.983729 * transformed + 0.4329510) + 0.238081;
    return clamp(outputTransform * (numerator / denominator), 0.0, 1.0);
}

vec3 LinearToDisplay(vec3 linearRadiance, float exposure)
{
    vec3 toneMapped = ACESToneMap(linearRadiance * max(exposure, 0.0));

    // Cerberus presents through an UNORM swapchain: explicit gamma, matching
    // SceneComposite.glsl.
    return pow(toneMapped, vec3(1.0 / 2.2));
}

// Reverse the raygen's diffuse/specular demodulation and add emissive back:
//   lit = diffuseIllum * diffuseAlbedo + specularIllum * envBRDF + emissive
// diffuseAlbedo is reconstructed from the G-buffer with the same formula and
// floor the raygen used; envBRDF comes straight from u_SpecularAlbedo (already
// clamped) so the round trip is exact. denoised == false takes the raw per-frame
// raygen illumination (Step 2 energy-check view); true takes the filtered split
// a-trous output (the denoiser-mode lit path).
vec3 Recombine(ivec2 pixel, bool denoised)
{
    vec3 diffuseIllum  = denoised
        ? imageLoad(u_SplitAtrousDiffuse, pixel).rgb
        : imageLoad(u_RadianceDiffuse, pixel).rgb;
    vec3 specularIllum = denoised
        ? imageLoad(u_SplitAtrousSpecular, pixel).rgb
        : imageLoad(u_RadianceSpecular, pixel).rgb;
    vec3 emissive = imageLoad(u_RadianceEmissive, pixel).rgb; // never filtered

    vec3 baseColor = imageLoad(u_Albedo, pixel).rgb;
    float metalness = imageLoad(u_Material, pixel).y;
    vec3 diffuseAlbedo = max(baseColor * (1.0 - metalness), vec3(CERBERUS_ALBEDO_FLOOR));
    // envBRDF is stable per pixel on a still camera now that the G-buffer-writing
    // primary sub-sample is fired dead centre (see the raygen), so the per-frame
    // value round-trips without flicker.
    vec3 envBRDF = imageLoad(u_SpecularAlbedo, pixel).rgb;

    return diffuseIllum * diffuseAlbedo + specularIllum * envBRDF + emissive;
}

vec3 DebugColor(ivec2 pixel)
{
    switch (u_Push.DebugView)
    {
        case 1u:
        {
            // Motion vector, scaled and centred on grey. Uniform tint under a
            // pan means the reprojection is consistent.
            vec2 m = imageLoad(u_MotionVector, pixel).xy;
            return vec3(m * 20.0 + 0.5, 0.5);
        }
        case 2u:
            return imageLoad(u_NormalDepth, pixel).xyz * 0.5 + 0.5;
        case 3u:
        {
            // Linear view-space depth, banded every 10 units.
            float z = imageLoad(u_NormalDepth, pixel).w;
            return vec3(fract(z * 0.1));
        }
        case 4u:
            return imageLoad(u_Albedo, pixel).rgb;
        case 5u:
        {
            uint id = floatBitsToUint(imageLoad(u_PositionMeshID, pixel).w);
            if (id == 0xFFFFFFFFu)
                return vec3(0.0);
            uint h = id * 2654435761u;
            return vec3(
                float((h >>  0) & 255u),
                float((h >>  8) & 255u),
                float((h >> 16) & 255u)) / 255.0;
        }
        case 6u:
        {
            // Denoiser variance (the a-trous output carries it in .a). Should be
            // dark where the image has converged and only bright at edges /
            // disocclusion. Bright everywhere on a still camera means the
            // temporal accumulation is not converging.
            float v = imageLoad(u_Source, pixel).a;
            return vec3(clamp(sqrt(max(v, 0.0)), 0.0, 1.0));
        }
        case 7u:
        {
            // Material: red = roughness, green = metalness, blue = the
            // "specular tightness" the denoiser derives (how gently it filters).
            vec2 m = imageLoad(u_Material, pixel).xy;
            float tightness = clamp((1.0 - m.x) * (0.3 + 0.7 * m.y), 0.0, 1.0);
            return vec3(m.x, m.y, tightness);
        }
        // Diffuse/specular split diagnostics (indices 8-10 in kDebugViews).
        case 8u:
            // Split recombined from the RAW per-frame channels (no denoise).
            // Energy check: must match "Lit" in reference mode.
            return LinearToDisplay(Recombine(pixel, false), u_Push.Exposure);
        case 9u:
        {
            // Specular temporal blend factor (.w) and history length (.z).
            //   R = applied alpha * 50  -> white if alpha >= 0.02 (NOT converging;
            //       something forces a short window), dark grey ~0.1 if alpha
            //       rode 1/historyLength down to ~0.002 (converged, stable).
            //   G = historyLength / 512  -> white if >= 512.
            vec4 m = (u_Push.SplitParity == 0u)
                ? imageLoad(u_SplitMomentsSpec0, pixel)
                : imageLoad(u_SplitMomentsSpec1, pixel);
            return vec3(clamp(m.w * 50.0, 0.0, 1.0), clamp(m.z / 512.0, 0.0, 1.0), 0.0);
        }
        case 10u:
            // Raw specular temporal EMA -- the demodulated illumination straight
            // out of SVGFTemporalSplit, no remodulation, no a-trous. If this is
            // stable on a still camera the flicker is in the remod (envBRDF);
            // if it flickers here the temporal EMA itself is the bug.
            return LinearToDisplay(imageLoad(u_SplitTemporalSpecular, pixel).rgb, u_Push.Exposure);
        default:
            return vec3(0.0);
    }
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(u_Source);
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    if (u_Push.DebugView != 0u)
    {
        imageStore(u_Output, pixel, vec4(DebugColor(pixel), 1.0));
        return;
    }

    vec3 linearRadiance = (u_Push.Remodulate != 0u)
        ? Recombine(pixel, true)                 // denoiser: filtered split
        : imageLoad(u_Source, pixel).rgb;        // reference: lit progressive mean
    imageStore(u_Output, pixel, vec4(LinearToDisplay(linearRadiance, u_Push.Exposure), 1.0));
}
