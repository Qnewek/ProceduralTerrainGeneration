#version 450 core
//Temporal accumulation of the clouds (Clouds.cpp): every texel of this frame's march (cloud_compute.shader) finds where its clouds were
//in the previous frame (the point at its scattering distance, projected with the previous view-projection), takes the history there,
//clamps it to the range of the march around the texel (3 x 3, so moved or uncovered clouds do not smear) and blends a share of the march
//into it. The dithering noise of the march averages out over the frames

layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba16f, binding = 0) uniform writeonly image2D resolved;
layout(rgba16f, binding = 1) uniform readonly image2D march;
layout(r32f, binding = 2) uniform readonly image2D marchDistance;
layout(binding = 11) uniform sampler2D history;

uniform mat4 inverseViewProjection;
uniform mat4 previousViewProjection;
uniform vec3 cameraPosition;      //world units
uniform float unitsPerMetre;      //world units per metre of the cloud layer (FrameBlock::cloudWind.z)
uniform bool historyValid;

//Share of this frame in the result: about the last 10 frames count
const float BLEND = 0.1;
//Distance of a texel whose ray misses the cloud layer or ends at the sky behind the scene (cloud_compute.shader: MAX_DISTANCE)
const float NO_CLOUD_DISTANCE = 60000.0;

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(resolved);
    if (any(greaterThanEqual(texel, size))) {
        return;
    }
    vec4 current = imageLoad(march, texel);
    float rayDistance = imageLoad(marchDistance, texel).r;
    //Without the layer the march is exact (no noise) and must stay empty: history from cloudy neighbours would tint the sky over the scene
    if (!historyValid || rayDistance >= NO_CLOUD_DISTANCE) {
        imageStore(resolved, texel, current);
        return;
    }

    vec2 uv = (vec2(texel) + 0.5) / vec2(size);
    vec4 farPoint = inverseViewProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - cameraPosition);
    vec3 world = cameraPosition + dir * rayDistance * unitsPerMetre;
    vec4 previous = previousViewProjection * vec4(world, 1.0);
    vec2 previousUv = previous.xy / previous.w * 0.5 + 0.5;
    if (previous.w <= 0.0 || any(lessThan(previousUv, vec2(0.0))) || any(greaterThan(previousUv, vec2(1.0)))) {
        imageStore(resolved, texel, current);
        return;
    }

    vec4 low = current, high = current;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec4 n = imageLoad(march, clamp(texel + ivec2(x, y), ivec2(0), size - 1));
            low = min(low, n);
            high = max(high, n);
        }
    }
    vec4 past = clamp(texture(history, previousUv), low, high);
    imageStore(resolved, texel, mix(past, current, BLEND));
}
