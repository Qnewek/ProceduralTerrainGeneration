//Fog of everything drawn in the 3D view (FrameBlock::fogOn): haze whose density falls with height, so valleys and the distance fade
//into the sky, then a fade into the sky over the last part of the render distance, which hides the edge of the world

#include "frame.glsl"
#include "sky.glsl"

//@param worldPos - the shaded point, in the space of viewPos
vec3 applyFog(vec3 color, vec3 worldPos)
{
    if (!fogOn) {
        return color;
    }
    vec3 toFragment = worldPos - viewPos;
    float rayDistance = length(toFragment);
    vec3 sky = fogColor(toFragment, light.direction);
    //Optical depth of density hazeDensity * exp(-hazeFalloff * y) along the ray from the camera: exact for an exponential
    float k = hazeFalloff * toFragment.y;
    float along = abs(k) > 1e-4 ? (1.0 - exp(-k)) / k : 1.0;
    float opticalDepth = hazeDensity * exp(-hazeFalloff * viewPos.y) * along * rayDistance;
    color = mix(sky, color, exp(-opticalDepth));
    return mix(color, sky, smoothstep(fogStart, fogDistance, rayDistance));
}
