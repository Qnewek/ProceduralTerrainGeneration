//Sky colors and the light of the sun and of the sky, read from the sky-view look-up table (Sky.cpp, AtmosphereShaders/skyview_compute.shader).
//Shared by the sky (sky_fragment.shader), the water reflections, the fog (fog.glsl) and the lighting of everything.
//sunDirection: unit vector from the ground towards the sun. The table is linear light; colors given here are display colors
//(tone mapped), like the rest of the lighting

#include "atmosphere_model.glsl"

layout(binding = 14) uniform sampler2D skyViewLut;     //Sky::skyViewUnit

//Scale of the sky radiance before tone mapping
const float SKY_EXPOSURE = 60.0;
//Night: the sky and the light that stays when the sun is gone (stars, the moon), display colors
const vec3 NIGHT_SKY = vec3(0.010, 0.014, 0.040);
const vec3 NIGHT_AMBIENT = vec3(0.30, 0.36, 0.55);

vec3 skyToDisplay(vec3 radiance)
{
    return 1.0 - exp(-radiance * SKY_EXPOSURE);
}

//0 at night, 1 in full day; dusk lies between
float daylight(vec3 sunDirection)
{
    return smoothstep(-0.12, 0.3, sunDirection.y);
}

//Row SKY_COLORS_ROW of the table: 0 = sunlight tint, 1 = ambient tint (both linear, 1 = white), 2 = transmittance towards the sun
vec3 skyLightColor(int index)
{
    return texelFetch(skyViewLut, ivec2(index, SKY_COLORS_ROW), 0).rgb;
}

//Multiplier of the sunlight color: the light left after the air between the sun and the ground; white high up, orange and dimmer
//near the horizon, none below it
vec3 sunlightTint(vec3 sunDirection)
{
    return skyLightColor(0) * smoothstep(-0.01, 0.04, sunDirection.y);
}

//Multiplier of the ambient light: the light of the whole sky, 1 for a sun 45 degrees high; dimmer and warmer at dusk, never darker than the night.
//Its color shift is halved: the Ambient color of the UI is already the color of a day sky
vec3 ambientTint(vec3 sunDirection)
{
    vec3 tint = skyLightColor(1);
    tint = mix(vec3(dot(tint, vec3(0.2126, 0.7152, 0.0722))), tint, 0.5);
    return max(tint, NIGHT_AMBIENT);
}

//Sky seen along a direction, without the sun disc; below the horizon it fades into a dark ground haze
vec3 skyColor(vec3 dir, vec3 sunDirection)
{
    dir = normalize(dir);
    float elevation = asin(clamp(dir.y, 0.0, 1.0));
    //Azimuth away from the sun: the table holds one half of the sky, the other is its mirror image
    vec2 horizontal = dir.xz, sunHorizontal = sunDirection.xz;
    float cosAzimuth = dot(horizontal, horizontal) > 1e-10 && dot(sunHorizontal, sunHorizontal) > 1e-10
        ? dot(normalize(horizontal), normalize(sunHorizontal)) : 1.0;
    float u = acos(clamp(cosAzimuth, -1.0, 1.0)) / PI;
    float v = skyViewV(elevation);
    vec2 uv = vec2((u * float(SKY_VIEW_SIZE.x - 1) + 0.5) / float(SKY_VIEW_SIZE.x),
                   (v * float(SKY_VIEW_SIZE.y - 1) + 0.5) / float(SKY_VIEW_ROWS));
    vec3 color = skyToDisplay(texture(skyViewLut, uv).rgb) + NIGHT_SKY * (1.0 - daylight(sunDirection));
    if (dir.y < 0.0) {
        color = mix(color, color * 0.35, smoothstep(0.0, 0.25, -dir.y));
    }
    return color;
}

//Sun disc and its close halo, added on top of skyColor
vec3 sunDisc(vec3 dir, vec3 sunDirection)
{
    float c = dot(normalize(dir), sunDirection);
    float disc = smoothstep(0.99955, 0.99975, c);
    return sunlightTint(sunDirection) * (disc * 2.0 + pow(max(c, 0.0), 800.0) * 0.4);
}

//Color the terrain fades into: the sky at the horizon in the direction of the view
vec3 fogColor(vec3 viewDir, vec3 sunDirection)
{
    //Never below the horizon ("flat" is a GLSL keyword)
    vec3 level = vec3(viewDir.x, max(viewDir.y, 0.0), viewDir.z);
    return skyColor(dot(level, level) > 1e-8 ? level : vec3(1.0, 0.0, 0.0), sunDirection);
}
