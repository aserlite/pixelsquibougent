#version 410 core

out vec2 v_uv;

void main()
{
    vec2 pos = vec2(
        float((gl_VertexID & 1) * 4) - 1.0,
        float((gl_VertexID >> 1) * 4) - 1.0
    );
    v_uv        = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
