//Sun shadows (ShadowMap): cascaded depth maps from the sun, one array layer per cascade, times the shadows of the clouds (clouds.glsl).
//Needs frame.glsl. The cascade comes from the depth along the camera view; shadows fade out over the last part of the last cascade

#include "clouds.glsl"

layout(binding = 15) uniform sampler2DArrayShadow shadowMap;   //FrameUniforms::shadowUnit

//Share of the last cascade over which shadows fade out
const float SHADOW_FADE = 0.15;
//Share of every other cascade's depth range, at its far end, over which its shadow blends into the next cascade (no sharpness jump at the split)
const float CASCADE_BLEND = 0.1;

//Lit share of a point in one cascade (filtered taps)
//@param inside - false when the point lies outside the cascade's map (then 1 = lit)
float sampleCascade(int cascade, vec3 worldPos, vec3 normal, int pcfRadius, out bool inside)
{
    float texel = shadowTexels[cascade];
    //Surfaces turned away from the sun need the largest offset
    float facing = clamp(dot(normal, light.direction), 0.0, 1.0);
    vec3 p = worldPos + normal * texel * (1.0 + 1.5 * (1.0 - facing)) + light.direction * texel;
    vec3 s = (shadowMatrices[cascade] * vec4(p, 1.0)).xyz;
    inside = all(greaterThanEqual(s, vec3(0.0))) && all(lessThanEqual(s, vec3(1.0)));
    if (!inside) {
        return 1.0;
    }
    vec2 size = vec2(textureSize(shadowMap, 0).xy);
    float lit = 0.0;
    float taps = 0.0;
    for (int y = -pcfRadius; y <= pcfRadius; y++) {
        for (int x = -pcfRadius; x <= pcfRadius; x++) {
            lit += texture(shadowMap, vec4(s.xy + vec2(x, y) / size, float(cascade), s.z));
            taps += 1.0;
        }
    }
    return lit / taps;
}

//Sunlight reaching a point past the shadow casters: 1 = lit, 1 - shadowStrength = fully shadowed
float cascadeShadow(vec3 worldPos, vec3 normal, int pcfRadius)
{
    if (shadowCascades <= 0 || depthPass) {
        return 1.0;
    }
    float depth = -(view * vec4(worldPos, 1.0)).z;
    float last = shadowSplits[shadowCascades - 1];
    if (depth >= last) {
        return 1.0;
    }
    int cascade = 0;
    while (cascade < shadowCascades - 1 && depth > shadowSplits[cascade]) {
        cascade++;
    }
    bool inside;
    float lit = sampleCascade(cascade, worldPos, normal, pcfRadius, inside);
    if (!inside) {
        return 1.0;
    }
    if (cascade < shadowCascades - 1) {
        float end = shadowSplits[cascade];
        float start = cascade == 0 ? 0.0 : shadowSplits[cascade - 1];
        float blendStart = end - CASCADE_BLEND * (end - start);
        if (depth > blendStart) {
            bool nextInside;
            float next = sampleCascade(cascade + 1, worldPos, normal, pcfRadius, nextInside);
            if (nextInside) {
                lit = mix(lit, next, smoothstep(blendStart, end, depth));
            }
        }
    }
    float fade = smoothstep(last * (1.0 - SHADOW_FADE), last, depth);
    return 1.0 - shadowStrength * (1.0 - lit) * (1.0 - fade);
}

//Sunlight reaching a point: 1 = lit, 0 = none
//@param worldPos - world space (after modelScale), as FragPos
//@param normal - unit normal of the surface; the point is moved along it by about a texel of its cascade, against self-shadowing (acne)
//@param pcfRadius - 1 = 3 x 3 filtered taps (terrain), 0 = one filtered tap (cheap, small surfaces)
float sunShadow(vec3 worldPos, vec3 normal, int pcfRadius)
{
    if (depthPass) {
        return 1.0;
    }
    return cascadeShadow(worldPos, normal, pcfRadius) * cloudShadow(worldPos);
}
