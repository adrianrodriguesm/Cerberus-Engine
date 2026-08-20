#ifndef CERBERUS_RT_PATH_TRACER_GLSL
#define CERBERUS_RT_PATH_TRACER_GLSL

layout(std140, set = 0, binding = 8) uniform PathTracerData
{
    // x = samples accumulated before this dispatch
    // y = maximum path bounce count
    // z/w = unused
    uvec4 Parameters;

    // x = display exposure
    // y/z/w = unused
    vec4 Presentation;
} PathTracer;

#endif // CERBERUS_RT_PATH_TRACER_GLSL
