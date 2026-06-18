#type rayGen
#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 1) uniform accelerationStructureEXT TLAS;
layout(rgba32f, set = 0, binding = 16) writeonly uniform image2D RTOutput;

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
    vec4 Params;
} Camera;

struct HitPayload
{
    vec3 hitValue;
};

layout(location = 0) rayPayloadEXT HitPayload Payload;

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

void main()
{
    ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);

    Payload.hitValue = vec3(0.0);

    traceRayEXT(
        TLAS,
        gl_RayFlagsOpaqueEXT,
        0xFF,
        0,
        0,
        0,
        Camera.Position.xyz,
        0.001,
        GetCameraRayDirection(vec2(gl_LaunchIDEXT.xy), vec2(gl_LaunchSizeEXT.xy)),
        100000.0,
        0
    );

    imageStore(RTOutput, pixel, vec4(Payload.hitValue, 1.0));
}

#type miss
#version 460

#extension GL_EXT_ray_tracing : require

struct HitPayload
{
    vec3 hitValue;
};

layout(location = 0) rayPayloadInEXT HitPayload Payload;

void main()
{
    Payload.hitValue = vec3(0.02, 0.04, 0.08);
}

#type closestHit
#version 460

#extension GL_EXT_ray_tracing : require

struct HitPayload
{
    vec3 hitValue;
};

hitAttributeEXT vec2 HitAttribs;
layout(location = 0) rayPayloadInEXT HitPayload Payload;

void main()
{
    Payload.hitValue = vec3(1.0 - HitAttribs.x - HitAttribs.y, HitAttribs.x, HitAttribs.y);
}
