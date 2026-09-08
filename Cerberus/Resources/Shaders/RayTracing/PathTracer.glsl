#ifndef CERBERUS_RT_PATH_TRACER_GLSL
#define CERBERUS_RT_PATH_TRACER_GLSL

layout(std140, set = 0, binding = 8) uniform PathTracerData
{
    // x = samples accumulated before this dispatch
    // y = maximum path bounce count
    // z = monotonic denoiser frame index (temporal decorrelation)
    // w = 1 while the SVGF denoiser is active, 0 otherwise
    uvec4 Parameters;

    // x = display exposure
    // y = firefly luminance clamp (denoiser mode; <= 0 disables)
    // z/w = unused
    vec4 Presentation;
} PathTracer;

#endif // CERBERUS_RT_PATH_TRACER_GLSL
