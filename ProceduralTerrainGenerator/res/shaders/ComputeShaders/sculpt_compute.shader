#version 450 core

// One dab of a sculpt brush (GpuSculpt). The same formulas as sculpt::Apply in Sculpt.cpp (the CPU reference, --gpu-check compares both):
// a change in one needs the same change in the other.
// Every invocation is one texel of the square around the brush (area). Heights from before the dab come from a copy of the square
// widened by the reach of SMOOTH (before, its texel (0, 0) = map texel copyOrigin), so no texel reads what another one writes.

layout(local_size_x = 16, local_size_y = 16) in;

layout(r32f, binding = 0) uniform readonly image2D before;
layout(r32f, binding = 1) uniform writeonly image2D heights;

const int BRUSH_RAISE = 0;
const int BRUSH_LOWER = 1;
const int BRUSH_SMOOTH = 2;
const int BRUSH_FLATTEN = 3;
const int BRUSH_NOISE = 4;

uniform int brush;
uniform ivec2 mapSize;
uniform ivec2 areaOrigin;	// first texel and size of the square of texels within the radius, inside the map
uniform ivec2 areaSize;
uniform ivec2 copyOrigin;
uniform vec2 center;		// map texels
uniform float radius;
uniform float hardness;
uniform float amount;
uniform float target;		// FLATTEN
uniform int seed;			// NOISE; the bits of the uint seed
uniform float noiseScale;
uniform float limit;
uniform int spacing;		// SMOOTH: distance between the averaged texels

// Integer hash to [-1, 1]; uint arithmetic wraps the same way as uint32_t in C++
float hash(int x, int y, uint s)
{
	uint n = uint(x) * 0x27d4eb2du ^ uint(y) * 0x165667b1u ^ s * 0x9e3779b9u;
	n = (n ^ (n >> 15)) * 0x2c1b3c6du;
	n = (n ^ (n >> 12)) * 0x297a2d39u;
	n ^= n >> 15;
	return float(n & 0xFFFFFFu) / 16777215.0 * 2.0 - 1.0;
}

float valueNoise(vec2 p, uint s)
{
	vec2 i = floor(p);
	vec2 f = p - i;
	vec2 u = f * f * (3.0 - 2.0 * f);
	int x = int(i.x), y = int(i.y);
	float a = hash(x, y, s), b = hash(x + 1, y, s);
	float c = hash(x, y + 1, s), d = hash(x + 1, y + 1, s);
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float noiseAt(vec2 texel)
{
	vec2 p = texel / max(noiseScale, 1.0);
	uint s = uint(seed);
	return (2.0 / 3.0) * valueNoise(p, s) + (1.0 / 3.0) * valueNoise(p * 2.0 + 17.0, s + 1u);
}

float falloff(float d)
{
	if (d >= radius) {
		return 0.0;
	}
	if (hardness >= 1.0) {
		return 1.0;
	}
	float edge = max(hardness, 0.0) * radius;
	float t = clamp((d - edge) / (radius - edge), 0.0, 1.0);
	return 1.0 - t * t * (3.0 - 2.0 * t);
}

// Height from before the dab; outside the map the nearest map texel
float heightBefore(ivec2 p)
{
	return imageLoad(before, clamp(p, ivec2(0), mapSize - 1) - copyOrigin).r;
}

void main()
{
	ivec2 local = ivec2(gl_GlobalInvocationID.xy);
	if (local.x >= areaSize.x || local.y >= areaSize.y) {
		return;
	}
	ivec2 p = areaOrigin + local;
	float w = falloff(distance(vec2(p), center));
	if (w <= 0.0) {
		return;
	}
	float h = heightBefore(p);
	if (brush == BRUSH_RAISE) {
		h += amount * w;
	}
	else if (brush == BRUSH_LOWER) {
		h -= amount * w;
	}
	else if (brush == BRUSH_NOISE) {
		h += amount * w * noiseAt(vec2(p));
	}
	else if (brush == BRUSH_SMOOTH) {
		float sum = 0.0;
		for (int dy = -1; dy <= 1; dy++) {
			for (int dx = -1; dx <= 1; dx++) {
				sum += heightBefore(p + ivec2(dx, dy) * spacing);
			}
		}
		h += (sum / 9.0 - h) * min(amount * w, 1.0);
	}
	else if (brush == BRUSH_FLATTEN) {
		h += (target - h) * min(amount * w, 1.0);
	}
	imageStore(heights, p, vec4(clamp(h, -limit, limit)));
}
