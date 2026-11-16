// Rivers carved into the terrain height (rivers::RiverConfig), as the last step of the generation, after the biome modifiers.
// A pure function of the map position (and of the river maps), so chunks stay seamless.
// The including file provides: permAt / simplex2 (noise.glsl) and uniform float waterHeight (biomes.glsl).

const int RIVERS_NONE = 0, RIVERS_REGION = 1, RIVERS_NOISE = 2;
const int RIVER_TABLE = 6;		// permutation table of the river noise (GpuHeightGenerator.cpp)

uniform int riverMethod;		// RIVERS_*
uniform vec3 riverShape;		// bankWidth (texels), bankSlope (height per texel), depthPerWidth (height per texel of half width)
uniform vec4 riverRegion;		// RIVERS_REGION: map position of cell (0, 0), map texels between cells, borderFade (texels)
uniform vec4 riverNoise;		// RIVERS_NOISE: wavelength (texels), half width (texels), maxElevation, warp

// RIVERS_REGION (RiverMap): offset to the nearest course (xy, texels), distance to it, half width; surface of that river, lake surface
layout(binding = 20) uniform sampler2D riverMap;
layout(binding = 21) uniform sampler2D riverLevels;

// Height of the terrain at distance (texels) from a channel: a U-shaped bed below the water surface, banks rising from the water
// at bankSlope, the carving fading out bankWidth texels from the channel. Never raises the terrain
float carveChannel(float height, float distance, float halfWidth, float level, float strength)
{
	float w = max(halfWidth, 0.5);
	float target;
	if (distance < w) {
		float t = distance / w;
		target = level - riverShape.z * w * (1.0 - t * t);
	}
	else {
		target = level + (distance - w) * riverShape.y;
	}
	float fade = strength * (1.0 - smoothstep(w + 0.5 * riverShape.x, w + riverShape.x, distance));
	return mix(height, min(height, target), fade);
}

float carveRegionRivers(float height, vec2 position)
{
	vec2 cells = vec2(textureSize(riverMap, 0));
	vec2 cell = (position - riverRegion.xy) / riverRegion.z;
	// Fades out inside the border of the region (cell centers span [0, cells - 1])
	vec2 inside = min(cell, cells - 1.0 - cell) * riverRegion.z;
	float strength = smoothstep(0.0, riverRegion.w, min(inside.x, inside.y));
	if (strength <= 0.0) {
		return height;
	}
	vec2 uv = (cell + 0.5) / cells;
	vec4 river = textureLod(riverMap, uv, 0.0);
	float level = textureLod(riverLevels, uv, 0.0).x;
	// The interpolated offset is exact next to a straight course; where the nearest course changes (between two rivers) it shrinks
	// towards 0, but the interpolated distance there is large and overestimates the true one by less than a cell
	float distance = max(length(river.xy), river.z - riverRegion.z);
	return carveChannel(height, distance, river.w, level, strength);
}

float riverNoiseValue(vec2 position)
{
	vec2 p = position / riverNoise.x;
	vec2 warp = vec2(simplex2(RIVER_TABLE, p.x * 0.5 + 31.7, p.y * 0.5 - 12.3), simplex2(RIVER_TABLE, p.x * 0.5 - 47.1, p.y * 0.5 + 8.9));
	vec2 q = p + riverNoise.w * warp;
	return simplex2(RIVER_TABLE, q.x, q.y);
}

float carveNoiseRivers(float height, vec2 position)
{
	// Only in the lowlands: the channels end where the land rises maxElevation above the water
	float strength = 1.0 - smoothstep(0.5 * riverNoise.z, riverNoise.z, height - waterHeight);
	if (strength <= 0.0) {
		return height;
	}
	// Distance to the zero line of the noise: value over the length of its gradient (one texel differences)
	float n = riverNoiseValue(position);
	vec2 gradient = vec2(riverNoiseValue(position + vec2(1.0, 0.0)) - n, riverNoiseValue(position + vec2(0.0, 1.0)) - n);
	float distance = abs(n) / max(length(gradient), 0.05 / riverNoise.x);
	return carveChannel(height, distance, riverNoise.y, waterHeight, strength);
}

float carveRivers(float height, vec2 position)
{
	if (riverMethod == RIVERS_REGION) {
		return clamp(carveRegionRivers(height, position), 0.0, 1.0);
	}
	if (riverMethod == RIVERS_NOISE) {
		return clamp(carveNoiseRivers(height, position), 0.0, 1.0);
	}
	return height;
}
