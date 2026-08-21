#version 450

layout(location = 0) out vec2 v_NDC;

void main()
{
    vec2 position = vec2(
        (gl_VertexIndex << 1) & 2,
        gl_VertexIndex & 2);
    v_NDC = position * 2.0 - 1.0;
    gl_Position = vec4(v_NDC, 0.0, 1.0);
}
