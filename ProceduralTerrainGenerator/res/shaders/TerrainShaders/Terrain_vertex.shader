#version 450 core
layout (location = 0) in vec3 aPos;     //y = 0 on the surface, -1 at the bottom of a skirt
layout (location = 1) in vec2 aTex;

out vec2 TexCoord;

uniform float meshScale;    //scales the mesh in x and z; a chunk of a coarser level reuses the mesh of the finest one

void main()
{
    gl_Position = vec4(aPos.x * meshScale, aPos.y, aPos.z * meshScale, 1.0);
    TexCoord = aTex;
}
