#version 450 core
//The cloud layer seen from the camera, at a lower resolution than the viewport (Clouds.cpp): a march through the slab of clouds along the
//ray of every texel, lit by the sun (a short march towards it, two-lobe phase) and by the sky. Output: rgb = light from the clouds
//(display colors, premultiplied), a = transmittance; the scene behind becomes scene * a + rgb.
//limitByDepth: the ray ends at the scene (copied depth), texels of the sky are left empty - for a camera at or above the clouds,
//over the drawn scene; without it the ray goes to the far end of the layer - for the sky, drawn before the scene

layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba16f, binding = 0) uniform writeonly image2D cloudImage;
layout(binding = 11) uniform sampler3D shapeNoise;
layout(binding = 12) uniform sampler3D detailNoise;
layout(binding = 13) uniform sampler2D sceneDepth;

uniform mat4 inverseViewProjection;
uniform mat4 inverseProjection;
uniform bool limitByDepth;
uniform int frameIndex;

#include "../common/frame.glsl"
#include "../common/sky.glsl"
#include "../common/clouds.glsl"

const float SHAPE_TILE = 2400.0;        //metres covered by the shape noise
const float DETAIL_TILE = 280.0;
const float EXTINCTION = 0.035;         //per metre at density 1 (cumulus: a mean free path of tens of metres)
const float MAX_DISTANCE = 60000.0;     //metres; the clouds have faded into the sky long before
const float FADE_DISTANCE = 25000.0;    //distance over which far clouds fade into the sky by e
const int MAX_STEPS = 96;
const int LIGHT_STEPS = 6;
const float SUN_INTENSITY = 1.6;

float heightFraction(vec3 p)
{
    return clamp((p.y - cloudLayer.x) / (cloudLayer.y - cloudLayer.x), 0.0, 1.0);
}

//Density 0..cloudWind.w at p (metres); without detail for the light march
float cloudDensity(vec3 p, bool detail)
{
    float h = heightFraction(p);
    vec2 weather = cloudWeatherAt(p.xz);
    float coverage = cloudCoverageAt(weather.r);
    if (coverage <= 0.0) {
        return 0.0;
    }
    //Rounded bases, tops that thin out: flat clouds keep to the lower part of the layer, tall ones fill it
    float top = mix(0.45, 1.0, weather.g);
    float profile = smoothstep(0.0, 0.08, h) * (1.0 - smoothstep(top * 0.6, top, h));
    //The billows move a little faster than the cover, so the clouds change as they drift
    vec3 shapePosition = (p + vec3(cloudWind.x, 0.0, cloudWind.y) * 0.3) / SHAPE_TILE;
    vec4 shape = texture(shapeNoise, shapePosition);
    float fbm = shape.g * 0.625 + shape.b * 0.25 + shape.a * 0.125;
    float base = cloudRemap(shape.r, fbm - 1.0, 1.0, 0.0, 1.0) * profile;
    //Even in the middle of a cluster the noise cuts the cloud into billows (coverage below 1)
    float local = coverage * 0.6;
    float density = clamp(cloudRemap(base, 1.0 - local, 1.0, 0.0, 1.0), 0.0, 1.0) * coverage;
    if (density <= 0.0) {
        return 0.0;
    }
    if (detail) {
        vec3 d = texture(detailNoise, p / DETAIL_TILE + vec3(0.0, time * 0.002, 0.0)).rgb;
        float high = d.r * 0.625 + d.g * 0.25 + d.b * 0.125;
        //Wispy at the base, billowy at the top
        high = mix(high, 1.0 - high, clamp(h * 6.0, 0.0, 1.0));
        density = clamp(cloudRemap(density, high * 0.5, 1.0, 0.0, 1.0), 0.0, 1.0);
    }
    return density * cloudWind.w;
}

float henyeyGreenstein(float cosAngle, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * cosAngle, 1.5);   //times 4 pi: 1 = isotropic
}

//Sunlight reaching p through the cloud towards the sun
float sunTransmittance(vec3 p, vec3 sun)
{
    float opticalDepth = 0.0;
    float t = 0.0;
    float stepLength = 18.0;
    for (int i = 0; i < LIGHT_STEPS; i++) {
        float t1 = t + stepLength;
        opticalDepth += cloudDensity(p + sun * (0.5 * (t + t1)), false) * stepLength;
        t = t1;
        stepLength *= 1.9;
    }
    opticalDepth *= EXTINCTION;
    //Light scattered many times gets deeper into the cloud than the direct light (cheap multiple scattering)
    return max(exp(-opticalDepth), 0.75 * exp(-0.2 * opticalDepth));
}

//Distances along the ray where it is inside the slab, false if never
bool slab(vec3 origin, vec3 dir, out float enter, out float exit)
{
    float base = cloudLayer.x, top = cloudLayer.y;
    if (abs(dir.y) < 1e-5) {
        enter = 0.0;
        exit = origin.y > base && origin.y < top ? MAX_DISTANCE : -1.0;
        return exit > 0.0;
    }
    float tBase = (base - origin.y) / dir.y;
    float tTop = (top - origin.y) / dir.y;
    enter = max(min(tBase, tTop), 0.0);
    exit = min(max(tBase, tTop), MAX_DISTANCE);
    return exit > enter;
}

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(cloudImage);
    if (any(greaterThanEqual(texel, size))) {
        return;
    }
    vec2 uv = (vec2(texel) + 0.5) / vec2(size);
    vec4 farPoint = inverseViewProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - viewPos);
    vec3 origin = viewPos / cloudWind.z;

    float enter, exit;
    bool inside = slab(origin, dir, enter, exit);
    if (limitByDepth) {
        float depth = texture(sceneDepth, uv).r;
        if (depth >= 1.0) {
            inside = false;
        }
        else {
            vec4 viewPoint = inverseProjection * vec4(vec3(uv, depth) * 2.0 - 1.0, 1.0);
            exit = min(exit, length(viewPoint.xyz / viewPoint.w) / cloudWind.z);
        }
    }
    if (!inside || exit <= enter) {
        imageStore(cloudImage, texel, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }

    vec3 sun = light.direction;
    float cosAngle = dot(dir, sun);
    float phase = mix(henyeyGreenstein(cosAngle, 0.75), henyeyGreenstein(cosAngle, -0.25), 0.55);
    vec3 sunColor = sunlightTint(sun) * light.diffuse * SUN_INTENSITY;
    vec3 skyTop = skyColor(vec3(0.0, 1.0, 0.0), sun);
    vec3 skyHorizon = fogColor(dir, sun);

    float segment = exit - enter;
    int steps = int(clamp(segment / 25.0, 24.0, float(MAX_STEPS)));
    float stepLength = segment / float(steps);
    //Interleaved gradient noise: the steps start at a different point for every texel and frame, which turns banding into fine noise
    float jitter = fract(52.9829189 * fract(dot(vec2(texel) + float(frameIndex % 64) * 5.588238, vec2(0.06711056, 0.00583715))));

    vec3 scattered = vec3(0.0);
    float transmittance = 1.0;
    float weightedDistance = 0.0, weightSum = 0.0;
    for (int i = 0; i < steps; i++) {
        float t = enter + (float(i) + jitter) * stepLength;
        vec3 p = origin + dir * t;
        float density = cloudDensity(p, true);
        if (density <= 0.0) {
            continue;
        }
        float h = heightFraction(p);
        //The sky lights the tops, the dark ground less the bases
        vec3 ambient = mix(skyHorizon * 0.75, skyTop, h);
        vec3 luminance = sunColor * sunTransmittance(p, sun) * phase + ambient;
        //Soft roll-off instead of clipping: sunlit tops seen from above stay shaded, not flat white
        luminance = 1.0 - exp(-luminance * 2.0);
        float stepTransmittance = exp(-density * EXTINCTION * stepLength);
        scattered += transmittance * luminance * (1.0 - stepTransmittance);
        weightedDistance += t * transmittance * (1.0 - stepTransmittance);
        weightSum += transmittance * (1.0 - stepTransmittance);
        transmittance *= stepTransmittance;
        if (transmittance < 0.01) {
            break;
        }
    }
    //Far clouds fade into the sky behind them
    if (weightSum > 0.0) {
        float fade = exp(-(weightedDistance / weightSum) / FADE_DISTANCE);
        scattered *= fade;
        transmittance = transmittance + (1.0 - transmittance) * (1.0 - fade);
    }
    imageStore(cloudImage, texel, vec4(scattered, transmittance));
}
