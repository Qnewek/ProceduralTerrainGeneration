#version 450 core
//Tiling noises of the clouds (Clouds.cpp), made once:
//target 0 - shape (3D): r = Perlin-Worley (billowy blobs), g, b, a = Worley fBm of rising frequency;
//target 1 - detail (3D): r, g, b = Worley fBm of rising frequency, to erode the edges;
//target 2 - weather map (2D): r = coverage, g = cloud type (0 = flat, 1 = tall)

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(rgba8, binding = 0) uniform writeonly image3D volume;
layout(rgba8, binding = 1) uniform writeonly image2D weatherMap;

uniform int target;

vec3 hash33(vec3 p)
{
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)), dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453);
}

//Gradient noise in about [-1, 1] that repeats every period cells
float gradientNoise(vec3 x, float period)
{
    vec3 i = floor(x);
    vec3 f = fract(x);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float corners[8];
    for (int c = 0; c < 8; c++) {
        vec3 o = vec3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
        vec3 g = normalize(hash33(mod(i + o, period)) * 2.0 - 1.0);
        corners[c] = dot(g, f - o);
    }
    float x0 = mix(mix(corners[0], corners[1], u.x), mix(corners[2], corners[3], u.x), u.y);
    float x1 = mix(mix(corners[4], corners[5], u.x), mix(corners[6], corners[7], u.x), u.y);
    return mix(x0, x1, u.z) * 1.6;
}

//1 at the feature points, 0 a cell away; repeats every period cells
float worley(vec3 x, float period)
{
    vec3 i = floor(x);
    vec3 f = fract(x);
    float nearest = 1.0;
    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int xx = -1; xx <= 1; xx++) {
                vec3 o = vec3(xx, y, z);
                vec3 point = o + hash33(mod(i + o, period));
                nearest = min(nearest, length(point - f));
            }
        }
    }
    return 1.0 - nearest;
}

float worleyFbm(vec3 p, float cells)
{
    return worley(p * cells, cells) * 0.625 + worley(p * cells * 2.0, cells * 2.0) * 0.25 + worley(p * cells * 4.0, cells * 4.0) * 0.125;
}

//In [0, 1]
float perlinFbm(vec3 p, float cells, int octaves)
{
    float sum = 0.0, amplitude = 0.5, total = 0.0;
    for (int o = 0; o < octaves; o++) {
        sum += gradientNoise(p * cells, cells) * amplitude;
        total += amplitude;
        cells *= 2.0;
        amplitude *= 0.5;
    }
    return clamp(sum / total * 0.5 + 0.5, 0.0, 1.0);
}

float remap(float value, float low, float high, float newLow, float newHigh)
{
    return newLow + (value - low) / (high - low) * (newHigh - newLow);
}

void main()
{
    ivec3 texel = ivec3(gl_GlobalInvocationID);
    if (target == 2) {
        ivec2 size = imageSize(weatherMap);
        if (texel.z != 0 || any(greaterThanEqual(texel.xy, size))) {
            return;
        }
        vec3 p = vec3((vec2(texel.xy) + 0.5) / vec2(size), 0.0);
        //Clusters of clouds: low frequency noise sharpened by Worley cells, so there are clear gaps between them
        float base = perlinFbm(p, 6.0, 5);
        float cells = worleyFbm(p, 5.0);
        float coverage = clamp(remap(base, cells * 0.6 - 0.1, 1.0, 0.0, 1.0), 0.0, 1.0);
        float type = perlinFbm(p + vec3(0.0, 0.0, 0.37), 3.0, 3);
        imageStore(weatherMap, texel.xy, vec4(coverage, smoothstep(0.35, 0.7, type), 0.0, 1.0));
        return;
    }
    ivec3 size = imageSize(volume);
    if (any(greaterThanEqual(texel, size))) {
        return;
    }
    vec3 p = (vec3(texel) + 0.5) / vec3(size);
    if (target == 0) {
        float perlin = perlinFbm(p, 4.0, 4);
        float cells = worleyFbm(p, 4.0);
        float perlinWorley = clamp(remap(perlin, 0.0, 1.0, cells, 1.0), 0.0, 1.0);
        imageStore(volume, texel, vec4(perlinWorley, cells, worleyFbm(p, 8.0), worleyFbm(p, 16.0)));
    }
    else {
        imageStore(volume, texel, vec4(worleyFbm(p, 2.0), worleyFbm(p, 4.0), worleyFbm(p, 8.0), 1.0));
    }
}
