#version 450 core
//Blades of grass around the camera on one ground chunk (GrassLayer). Every map texel (1 m²) has slots candidate blades at positions hashed
//from the texel and the slot, so a blade stays where it is from frame to frame. A blade grows if its random number is below the density:
//the grass share of the materials of the splat map (TerrainMaterial::grass), less on cliffs, thinned with distance. Kept blades in the view
//are appended to the blade buffer; the instance count of the draw command is the counter
layout(local_size_x = 64) in;

struct Blade {
    vec4 positionHeight;    //xyz = base (world units before modelScale), w = height
    vec4 data;              //x = facing angle, y = width, z = color (packUnorm4x8, a = random), w = lean
};
layout(std430, binding = 0) writeonly buffer Blades { Blade blades[]; };
//DrawArraysIndirectCommand: count, instanceCount (the counter), first, baseInstance
layout(std430, binding = 1) buffer Command { uint vertexCount; uint bladeCount; uint first; uint baseInstance; };

uniform sampler2D heightMap;
uniform sampler2D surfaceMap;
uniform sampler2DArray splatMap;

uniform int textureSize;        //of the chunk maps
uniform ivec2 firstTexel;       //map texel of texel (0, 0) of the maps
uniform int step;               //map texels per texel of the maps (2^level)
uniform ivec2 regionMin;        //map texels looked at: [regionMin, regionMin + regionSize)
uniform ivec2 regionSize;
uniform int slots;              //candidate blades per map texel
uniform vec2 mapOrigin;         //world XZ of map texel (0, 0)
uniform float worldUnitsPerTexel;
uniform float heightScale;
uniform vec3 eye;               //camera before modelScale
uniform vec4 planes[6];         //view frustum before modelScale
uniform uint capacity;

uniform float density;          //blades per m² on a material with grass 1
uniform float range;            //world units: no grass beyond
uniform float bladeHeight;      //world units
uniform float bladeWidth;
uniform vec2 cliffSlopes;       //radians: grass thins out between them
uniform vec4 materials[16];     //rgb = grass color, a = grass share
uniform int materialCount;

#include "../common/surface.glsl"

uint hash(uint x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float random(inout uint state)
{
    state = hash(state);
    return float(state >> 8) * (1.0 / 16777216.0);
}

bool outsideFrustum(vec3 center, float radius)
{
    for (int i = 0; i < 6; i++) {
        if (dot(planes[i].xyz, center) + planes[i].w < -radius) {
            return true;
        }
    }
    return false;
}

void main()
{
    uint index = gl_GlobalInvocationID.x;
    uint perRow = uint(regionSize.x * slots);
    if (index >= perRow * uint(regionSize.y)) {
        return;
    }
    int slot = int(index % uint(slots));
    ivec2 texel = regionMin + ivec2(int(index % perRow) / slots, int(index / perRow));

    uint state = hash(uint(texel.x) * 73856093U ^ uint(texel.y) * 19349663U ^ uint(slot) * 83492791U);
    vec2 mapPosition = vec2(texel) + vec2(random(state), random(state)) - 0.5;
    vec2 xz = mapOrigin + mapPosition * worldUnitsPerTexel;
    float flatDistance = distance(xz, eye.xz);
    if (flatDistance > range) {
        return;
    }
    //Fewer blades far away (they get wider instead), all of them up to a quarter of the range
    float near = 0.25 * range;
    float keep = flatDistance < near ? 1.0 : mix(1.0, 0.15, (flatDistance - near) / (range - near));
    float chance = random(state);
    //Slot k of slots holds the k-th blade of a texel: slots below the density always, the next one partly
    float slotShare = clamp(density - float(slot), 0.0, 1.0);
    if (chance >= slotShare * keep) {
        return;
    }

    vec2 uv = ((mapPosition - vec2(firstTexel)) / float(step) + 0.5) / float(textureSize);
    float share = 0.0, total = 0.0;
    vec3 color = vec3(0.0);
    for (int layer = 0; layer * 4 < materialCount; layer++) {
        vec4 weights = texture(splatMap, vec3(uv, float(layer)));
        for (int c = 0; c < 4; c++) {
            int m = layer * 4 + c;
            if (m < materialCount) {
                total += weights[c];
                share += weights[c] * materials[m].a;
                color += weights[c] * materials[m].a * materials[m].rgb;
            }
        }
    }
    if (share <= 0.0 || total <= 0.0) {
        return;
    }
    color /= share;
    share /= total;
    vec4 derivatives = texture(surfaceMap, uv);
    share *= 1.0 - smoothstep(cliffSlopes.x, cliffSlopes.y, surfaceSlope(derivatives, heightScale));
    //The same random number again: a blade appears and disappears as the share passes it, not at random
    if (chance >= slotShare * keep * share) {
        return;
    }

    float height = bladeHeight * mix(0.6, 1.3, random(state));
    //Shorter towards the end of the range, so the grass ends without a line
    height *= 1.0 - smoothstep(0.8 * range, range, flatDistance);
    vec3 base = vec3(xz.x, texture(heightMap, uv).r * heightScale, xz.y);
    if (height <= 0.0 || outsideFrustum(base + vec3(0.0, 0.5 * height, 0.0), height)) {
        return;
    }
    uint slotIndex = atomicAdd(bladeCount, 1u);
    if (slotIndex >= capacity) {
        return;
    }
    float brightness = mix(0.8, 1.15, random(state));
    Blade blade;
    blade.positionHeight = vec4(base, height);
    blade.data = vec4(random(state) * 6.2831853, bladeWidth / sqrt(keep) * mix(0.8, 1.2, random(state)),
        uintBitsToFloat(packUnorm4x8(vec4(clamp(color * brightness, 0.0, 1.0), random(state)))), mix(0.1, 0.45, random(state)));
    blades[slotIndex] = blade;
}
