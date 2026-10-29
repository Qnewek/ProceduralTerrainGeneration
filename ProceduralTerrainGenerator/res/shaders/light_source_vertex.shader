#version 330 core

layout(location = 0) in vec3 aPos;

#include "common/transform.glsl"

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0f);
}