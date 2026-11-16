// Rivers (rivers::RiverConfig): carved into the terrain height as the last step of the generation (after the biome modifiers), more humidity
// for the biomes near them and a bed material in the splat map. A pure function of the map position (and of the river maps), so chunks
// stay seamless. The including file provides: permAt / simplex2 (noise.glsl) and uniform float waterHeight (biomes.glsl).
// Use: Channel c = channelAt(position) once per texel (only if riverMethod != RIVERS_NONE), then the functions below.

const int RIVERS_NONE = 0, RIVERS_REGION = 1, RIVERS_NOISE = 2;
const int RIVER_TABLE = 6;		// permutation table of the river noise (GpuHeightGenerator.cpp)

uniform int riverMethod;		// RIVERS_*
uniform vec3 riverShape;		// bankWidth (texels), bankSlope (height per texel), depthPerWidth (height per texel of half width)
uniform vec4 riverRegion;		// RIVERS_REGION: map position of cell (0, 0), map texels between cells, borderFade (texels)
uniform vec4 riverNoise;		// RIVERS_NOISE: wavelength (texels), half width (texels), maxElevation, warp
uniform vec2 riverClimate;		// humidity added at a channel, distance (texels) from its edge over which it fades out
uniform int riverBedMaterial;	// material of the bed in the splat map, -1 = none

// RIVERS_REGION (RiverMap): offset to the nearest course (xy, texels), distance to it, half width; surface of that river, lake surface
layout(binding = 20) uniform sampler2D riverMap;
layout(binding = 21) uniform sampler2D riverLevels;

// The nearest channel of a texel
struct Channel {
	float distance;		// to the course, texels
	float halfWidth;	// texels
	float level;		// water surface, terrain height
	float strength;		// 0 = no river here (outside the region); the height masks of the noise method are applied later
};

float riverNoiseValue(vec2 position)
{
	vec2 p = position / riverNoise.x;
	vec2 warp = vec2(simplex2(RIVER_TABLE, p.x * 0.5 + 31.7, p.y * 0.5 - 12.3), simplex2(RIVER_TABLE, p.x * 0.5 - 47.1, p.y * 0.5 + 8.9));
	vec2 q = p + riverNoise.w * warp;
	return simplex2(RIVER_TABLE, q.x, q.y);
}

Channel channelAt(vec2 position)
{
	Channel c;
	if (riverMethod == RIVERS_REGION) {
		vec2 cells = vec2(textureSize(riverMap, 0));
		vec2 cell = (position - riverRegion.xy) / riverRegion.z;
		// Fades out inside the border of the region (cell centers span [0, cells - 1])
		vec2 inside = min(cell, cells - 1.0 - cell) * riverRegion.z;
		c.strength = smoothstep(0.0, riverRegion.w, min(inside.x, inside.y));
		vec2 uv = (cell + 0.5) / cells;
		vec4 river = textureLod(riverMap, uv, 0.0);
		// The interpolated offset is exact next to a straight course; where the nearest course changes (between two rivers) it shrinks
		// towards 0, but the interpolated distance there is large and overestimates the true one by less than a cell
		c.distance = max(length(river.xy), river.z - riverRegion.z);
		c.halfWidth = max(river.w, 0.5);
		c.level = textureLod(riverLevels, uv, 0.0).x;
	}
	else {
		// Distance to the zero line of the noise: value over the length of its gradient (one texel differences)
		float n = riverNoiseValue(position);
		vec2 gradient = vec2(riverNoiseValue(position + vec2(1.0, 0.0)) - n, riverNoiseValue(position + vec2(0.0, 1.0)) - n);
		c.distance = abs(n) / max(length(gradient), 0.05 / riverNoise.x);
		c.halfWidth = max(riverNoise.y, 0.5);
		c.level = waterHeight;
		c.strength = 1.0;
	}
	return c;
}

// Strength of the channel over terrain of this height: noise rivers only in the lowlands (they end maxElevation above the water) and on land
// (the sea floor is not carved; faded over the depth of a channel below the surface, so the mouth meets the sea floor without a step)
float riverStrength(Channel c, float height)
{
	if (riverMethod != RIVERS_NOISE) {
		return c.strength;
	}
	float above = height - waterHeight;
	float mouthDepth = max(riverShape.z * riverNoise.y, 1e-4);
	return (1.0 - smoothstep(0.5 * riverNoise.z, riverNoise.z, above)) * smoothstep(-mouthDepth, 0.0, above);
}

// Height of the terrain with the channel: a U-shaped bed below the water surface, banks rising from the water at bankSlope, the carving
// fading out bankWidth texels from the channel. Never raises the terrain; the result is in [0, 1]
float carveRivers(float height, Channel c)
{
	if (riverMethod == RIVERS_NONE) {
		return height;
	}
	float strength = riverStrength(c, height);
	if (strength <= 0.0) {
		return height;
	}
	float w = c.halfWidth;
	float target;
	if (c.distance < w) {
		float t = c.distance / w;
		target = c.level - riverShape.z * w * (1.0 - t * t);
	}
	else {
		target = c.level + (c.distance - w) * riverShape.y;
	}
	float fade = strength * (1.0 - smoothstep(w + 0.5 * riverShape.x, w + riverShape.x, c.distance));
	return clamp(mix(height, min(height, target), fade), 0.0, 1.0);
}

// Humidity a river adds to the biomes: riverClimate.x at the channel, fading out riverClimate.y texels from its edge
float riverHumidity(Channel c, float height)
{
	if (riverMethod == RIVERS_NONE) {
		return 0.0;
	}
	return riverClimate.x * riverStrength(c, height) * (1.0 - smoothstep(c.halfWidth, c.halfWidth + riverClimate.y, c.distance));
}

// Share of the bed material: the channel and a little beyond its edge
float riverBedShare(Channel c, float height)
{
	if (riverMethod == RIVERS_NONE || riverBedMaterial < 0) {
		return 0.0;
	}
	return riverStrength(c, height) * (1.0 - smoothstep(c.halfWidth, c.halfWidth * 1.3 + 1.0, c.distance));
}
