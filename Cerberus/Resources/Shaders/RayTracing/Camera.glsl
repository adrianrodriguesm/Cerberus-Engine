#ifndef CERBERUS_RT_CAMERA_GLSL
#define CERBERUS_RT_CAMERA_GLSL

layout(set = 0, binding = 2) uniform CameraData
{
    mat4 Projection;
    mat4 View;
    mat4 ViewProjection;
    mat4 InverseView;
    mat4 InverseProjection;
    mat4 InverseViewReverseProjection;

    vec4 Position;
    vec4 Forward;
    vec4 Right;
    vec4 Up;

    // x = tanHalfFovY
    // y = aspect
    // z/w = unused
    vec4 Params;
} Camera;

vec3 GetCameraRayDirection(vec2 launchId, vec2 launchSize)
{
    vec2 uv = (launchId + vec2(0.5)) / launchSize;
    vec2 screen = uv * 2.0 - 1.0;

    float tanHalfFovY = Camera.Params.x;
    float aspect = Camera.Params.y;

    return normalize(
        Camera.Forward.xyz +
        Camera.Right.xyz * screen.x * aspect * tanHalfFovY +
        Camera.Up.xyz * screen.y * tanHalfFovY
    );
}

#endif // CERBERUS_RT_CAMERA_GLSL
