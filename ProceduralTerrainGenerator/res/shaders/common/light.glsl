//Light reaching lit surfaces (FrameBlock): the sky, the fog and the ambient follow the sun (skySun.xyz); the directional light
//(light.direction) is the sun, or at night the moon (LightSource::GetLightDirection), whose light rises with skySun.w

#include "frame.glsl"
#include "sky.glsl"

//True while the directional light is the moon (it stands opposite the sun, so far from skySun)
bool moonIsLight()
{
    return dot(light.direction, skySun.xyz) < 0.99;
}

//Multiplier of light.diffuse for the directional light: the sunlight left after the air, or the moonlight, none below the horizon
vec3 directLightTint()
{
    if (moonIsLight()) {
        return MOON_LIGHT * skySun.w * smoothstep(-0.01, 0.04, light.direction.y);
    }
    return sunlightTint(light.direction);
}

//Multiplier of light.ambient: the light of the whole sky; under the moon the night floor is dimmer
vec3 ambientLightTint()
{
    return ambientTint(skySun.xyz, mix(1.0, MOON_NIGHT_AMBIENT, clamp(skySun.w, 0.0, 1.0)));
}
