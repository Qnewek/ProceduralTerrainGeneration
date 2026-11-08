#version 450 core

in vec2 ndc;
out vec4 FragColor;

uniform mat4 inverseViewProjection;     // inverse of projection * view, view without the camera translation
uniform vec3 sunDirection;              // unit vector towards the sun

#include "common/sky.glsl"

void main()
{
    vec4 farPoint = inverseViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w);
    FragColor = vec4(skyColor(dir, sunDirection) + sunDisc(dir, sunDirection), 1.0);
}
