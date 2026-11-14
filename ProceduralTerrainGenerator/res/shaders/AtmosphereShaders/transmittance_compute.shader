#version 450 core
//Transmittance look-up table: light left after passing from a point in the atmosphere to its top (atmosphere_model.glsl). Computed once

layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba16f, binding = 0) uniform writeonly image2D transmittanceLut;

#include "../common/atmosphere_model.glsl"

const int STEPS = 40;

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(transmittanceLut);
    if (any(greaterThanEqual(texel, size))) {
        return;
    }
    float r, mu;
    transmittanceParameters((vec2(texel) + 0.5) / vec2(size), r, mu);
    vec3 origin = vec3(0.0, r, 0.0);
    vec3 direction = vec3(sqrt(1.0 - mu * mu), mu, 0.0);
    float rayLength = max(raySphere(origin, direction, TOP_RADIUS), 0.0);
    vec3 depth = vec3(0.0);
    float stepLength = rayLength / float(STEPS);
    for (int i = 0; i < STEPS; i++) {
        vec3 p = origin + direction * (float(i) + 0.5) * stepLength;
        depth += mediumAt(max(0.0, length(p) - GROUND_RADIUS)).extinction * stepLength;
    }
    imageStore(transmittanceLut, texel, vec4(exp(-depth), 1.0));
}
