#version 450 core

// One triangle covering the whole viewport, made from gl_VertexID (no vertex buffer)
out vec2 ndc;

void main()
{
    ndc = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;   // (-1, -1), (3, -1), (-1, 3)
    gl_Position = vec4(ndc, 1.0, 1.0);
}
