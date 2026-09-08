#type compute
#version 450 core

// Reference-mode progressive accumulation. Consumes the raygen's raw per-frame
// linear radiance and maintains an unbiased running mean, exactly the math the
// raygen used to perform inline before CL-B split it out.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, rgba32f) readonly uniform image2D u_Radiance;
layout(binding = 1, rgba32f)          uniform image2D u_Accumulation;

layout(push_constant) uniform Push
{
    // Samples already represented by u_Accumulation. Zero on a reset, which makes
    // this dispatch ignore stale accumulation contents (no explicit clear).
    uint SampleIndex;
} u_Push;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(u_Accumulation);
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    vec3 radiance = imageLoad(u_Radiance, pixel).rgb;

    // Online mean: previous + (new - previous) / sampleCount.
    vec3 accumulated = radiance;
    if (u_Push.SampleIndex > 0u)
    {
        vec3 previous = imageLoad(u_Accumulation, pixel).rgb;
        float sampleCount = float(u_Push.SampleIndex + 1u);
        accumulated = previous + (radiance - previous) / sampleCount;
    }

    imageStore(u_Accumulation, pixel, vec4(accumulated, 1.0));
}
