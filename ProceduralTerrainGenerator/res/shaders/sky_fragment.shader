#version 450 core
//The sky behind everything: the sky-view table (common/sky.glsl), the sun disc, stars at night and the clouds (Clouds::RenderSky)

in vec2 ndc;
out vec4 FragColor;

uniform mat4 inverseViewProjection;     // inverse of projection * view, view without the camera translation
uniform vec3 sunDirection;              // unit vector towards the sun
uniform bool cloudsOn;
uniform vec4 viewportRect;              // xy = lower-left corner in window pixels, zw = size
layout(binding = 13) uniform sampler2D cloudImage;     // Clouds::imageUnit: rgb = light, a = transmittance

#include "common/sky.glsl"

float starHash(vec3 p)
{
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

//Stars on a grid of cells over the directions: one star at a random point of some cells, fixed to the sky
vec3 stars(vec3 dir)
{
    const float CELLS = 220.0;
    vec3 cell = floor(dir * CELLS);
    float present = starHash(cell);
    if (present < 0.92) {
        return vec3(0.0);
    }
    vec3 position = (cell + vec3(starHash(cell + 1.3), starHash(cell + 2.7), starHash(cell + 4.1))) / CELLS;
    float d = length(dir - normalize(position)) * CELLS;
    float brightness = pow((present - 0.92) / 0.08, 3.0);
    vec3 tint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.9, 0.75), starHash(cell + 7.7));
    return tint * brightness * (1.0 - smoothstep(0.0, 0.35, d));
}

void main()
{
    vec4 farPoint = inverseViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w);
    vec3 color = skyColor(dir, sunDirection) + sunDisc(dir, sunDirection);
    //Stars fade in as the sky darkens and sink into the haze at the horizon
    float night = 1.0 - smoothstep(-0.2, 0.0, sunDirection.y);
    color += stars(dir) * night * smoothstep(0.0, 0.15, dir.y);
    if (cloudsOn) {
        vec4 clouds = texture(cloudImage, (gl_FragCoord.xy - viewportRect.xy) / viewportRect.zw);
        color = color * clouds.a + clouds.rgb;
    }
    FragColor = vec4(color, 1.0);
}
