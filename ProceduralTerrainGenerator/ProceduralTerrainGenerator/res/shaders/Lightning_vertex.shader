#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out float Height;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float scale;
uniform float stretch;

void main()
{
    Height = aPos.y;
    FragPos = (view * model * vec4(aPos, 1.0)).xyz;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoords = aTexCoords;
    Normal = aNormal;
}