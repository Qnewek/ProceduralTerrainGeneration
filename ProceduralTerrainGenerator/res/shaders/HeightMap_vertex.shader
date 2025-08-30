#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;

out vec3 FragPos;
out vec3 Normal;
out vec3 Color;
out float Height;

uniform bool flatten;
uniform int size;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    if(flatten){
        FragPos = vec3(aPos.z/size, aPos.x/size, 0.0f);
        gl_Position = vec4(FragPos, 1.0);
        Normal = aNormal;
    }
    else{
        FragPos = (view * model * vec4(aPos, 1.0)).xyz;
        gl_Position = projection * view * model * vec4(aPos, 1.0);
        Normal = aNormal;
    }
    Color = aColor;
    Height = aPos.y;
}