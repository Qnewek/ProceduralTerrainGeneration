#version 450 core

// Hydraulic erosion with water on a grid: virtual pipes model (Mei, Decaudin, Hu: "Fast Hydraulic Erosion Simulation and Visualization on GPU", 2007)
// and thermal erosion. Lengths in texels (cell size 1, pipe length 1 and cross section 1); heights = map value * heightScale.
// Changes to the paper: sediment flows through the pipes with the water (the same share of it as of the water), so little material is lost
// (what the water carries over the edge of the map, and sediment that cannot settle at the end); the capacity grows with the flow (speed * depth),
// not the speed alone, so a thin film of water cannot carry more than a stream; dissolving never digs below the lowest neighbour (pits)
// and settling never builds above the highest neighbour + the talus height (spikes along streams).
// Every texel only writes its own texels and reads what no invocation of the same stage writes, so the result does not depend on the order of work.
// GpuErosion runs LOAD, then per iteration FLUX, WATER (src -> dst), THERMAL_OUT and THERMAL_APPLY if thermal erosion is on, and swaps src with dst;
// after the iterations a few more without rain, until the water dries up (sediment settles gradually); STORE at the end.
//
// src / dst: x = terrain b, y = water d, z = sediment s
// flux: outflow to the left, right, top and bottom neighbour (x - 1, x + 1, y - 1, y + 1)
// thermal: material sliding to the four neighbours (THERMAL_OUT -> THERMAL_APPLY)

layout(local_size_x = 16, local_size_y = 16) in;

const int STAGE_LOAD = 0;
const int STAGE_FLUX = 1;
const int STAGE_WATER = 2;
const int STAGE_THERMAL_OUT = 3;
const int STAGE_THERMAL_APPLY = 4;
const int STAGE_STORE = 5;

const float GRAVITY = 9.81;

layout(rgba32f, binding = 0) uniform image2D src;
layout(rgba32f, binding = 1) uniform image2D dst;
layout(rgba32f, binding = 2) uniform image2D flux;
layout(rgba32f, binding = 3) uniform image2D thermal;
layout(r32f, binding = 4) uniform image2D heightMap;	// LOAD reads it into src, STORE writes src to it

uniform int stage;
uniform ivec2 mapSize;
uniform float heightScale;
uniform float timeStep;
uniform float rainRate;
uniform float evaporationRate;
uniform float sedimentCapacity;
uniform float minTilt;
uniform float dissolvingRate;
uniform float depositionRate;
uniform float talusHeight;		// height difference between neighbours at the talus angle
uniform float thermalRate;

const ivec2 NEIGHBOURS[4] = ivec2[4](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));

bool inside(ivec2 p)
{
	return all(greaterThanEqual(p, ivec2(0))) && all(lessThan(p, mapSize));
}

// Water after the rain of this step; FLUX and WATER both use it, so it is not stored in between
float rainedWater(float d)
{
	return d + timeStep * rainRate;
}

// Outflow to the four neighbours from the difference of water surface heights, scaled down so that no more water leaves than there is.
// Outside the map lies ground as high as this texel without water, so water flows off the map
void fluxStage(ivec2 p)
{
	vec4 here = imageLoad(src, p);
	float d = rainedWater(here.y);
	float surface = here.x + d;
	vec4 f = imageLoad(flux, p);
	for (int i = 0; i < 4; i++) {
		ivec2 q = p + NEIGHBOURS[i];
		float neighbourSurface = here.x;
		if (inside(q)) {
			vec4 n = imageLoad(src, q);
			neighbourSurface = n.x + rainedWater(n.y);
		}
		f[i] = max(0.0, f[i] + timeStep * GRAVITY * (surface - neighbourSurface));
	}
	float total = f.x + f.y + f.z + f.w;
	if (total > 0.0) {
		f *= min(1.0, d / (total * timeStep));
	}
	imageStore(flux, p, f);
}

// Water and sediment neighbour q sends to p through its pipe towards p: the outflow and the same share of its sediment; 0 outside the map
vec2 inflowFrom(ivec2 q, int pipe)
{
	if (!inside(q)) {
		return vec2(0.0);
	}
	float water = imageLoad(flux, q)[pipe] * timeStep;
	if (water <= 0.0) {
		return vec2(0.0);
	}
	vec4 n = imageLoad(src, q);
	return vec2(water, n.z * min(1.0, water / rainedWater(n.y)));
}

float terrainAt(ivec2 q, float fallback)
{
	return inside(q) ? imageLoad(src, q).x : fallback;
}

// Water and sediment moved through the pipes, then dissolving or depositing by the capacity of the flow, then evaporation
void waterStage(ivec2 p)
{
	vec4 here = imageLoad(src, p);
	vec4 out4 = imageLoad(flux, p);
	vec2 fromLeft = inflowFrom(p + ivec2(-1, 0), 1);
	vec2 fromRight = inflowFrom(p + ivec2(1, 0), 0);
	vec2 fromTop = inflowFrom(p + ivec2(0, -1), 3);
	vec2 fromBottom = inflowFrom(p + ivec2(0, 1), 2);

	float d1 = rainedWater(here.y);
	float outflow = (out4.x + out4.y + out4.z + out4.w) * timeStep;	// at most d1, as FLUX scaled it
	vec2 inflow = fromLeft + fromRight + fromTop + fromBottom;
	float water = max(0.0, d1 + inflow.x - outflow);
	float sediment = (outflow > 0.0 ? here.z * (1.0 - min(1.0, outflow / d1)) : here.z) + inflow.y;

	// Water passing through the texel per unit of time (speed * depth)
	vec2 flow = 0.5 * vec2(fromLeft.x - out4.x * timeStep + out4.y * timeStep - fromRight.x,
		fromTop.x - out4.z * timeStep + out4.w * timeStep - fromBottom.x) / timeStep;

	// Slope of the terrain from the neighbours (one-sided at the edges)
	float terrain = here.x;
	float left = terrainAt(p + ivec2(-1, 0), terrain), right = terrainAt(p + ivec2(1, 0), terrain);
	float top = terrainAt(p + ivec2(0, -1), terrain), bottom = terrainAt(p + ivec2(0, 1), terrain);
	vec2 gradient = 0.5 * vec2(right - left, bottom - top);
	float sinTilt = length(gradient) / sqrt(1.0 + dot(gradient, gradient));

	float capacity = sedimentCapacity * max(sinTilt, minTilt) * length(flow);
	if (capacity > sediment) {
		// Never below the lowest neighbour: a texel dug deeper becomes a pit that gathers all the water and digs itself deeper.
		// A channel still deepens, as its bed goes down along the stream
		float lowestNeighbour = min(min(left, right), min(top, bottom));
		float dissolved = min(min(1.0, timeStep * dissolvingRate) * (capacity - sediment), max(0.0, terrain - lowestNeighbour));
		terrain -= dissolved;
		sediment += dissolved;
	}
	else {
		// Never above the highest neighbour by more than the talus height: water slowing down in a thin stream would drop all its sediment
		// on single texels, a line of spikes along the stream. What cannot settle here flows on
		float highestNeighbour = max(max(left, right), max(top, bottom));
		float deposited = min(min(1.0, timeStep * depositionRate) * (sediment - capacity), max(0.0, highestNeighbour + talusHeight - terrain));
		terrain += deposited;
		sediment -= deposited;
	}
	water *= max(0.0, 1.0 - timeStep * evaporationRate);
	imageStore(dst, p, vec4(terrain, water, sediment, 0.0));
}

// Material above the talus slope slides to lower neighbours: thermalRate * timeStep of half the largest excess leaves,
// split by the excess towards every neighbour
void thermalOutStage(ivec2 p)
{
	float height = imageLoad(dst, p).x;
	vec4 excess = vec4(0.0);
	for (int i = 0; i < 4; i++) {
		ivec2 q = p + NEIGHBOURS[i];
		if (inside(q)) {
			excess[i] = max(0.0, height - imageLoad(dst, q).x - talusHeight);
		}
	}
	float total = excess.x + excess.y + excess.z + excess.w;
	vec4 moved = vec4(0.0);
	if (total > 0.0) {
		float largest = max(max(excess.x, excess.y), max(excess.z, excess.w));
		moved = excess / total * (0.5 * largest * min(1.0, timeStep * thermalRate));
	}
	imageStore(thermal, p, moved);
}

void thermalApplyStage(ivec2 p)
{
	vec4 here = imageLoad(dst, p);
	vec4 out4 = imageLoad(thermal, p);
	float gained = 0.0;
	for (int i = 0; i < 4; i++) {
		ivec2 q = p + NEIGHBOURS[i];
		if (inside(q)) {
			// The neighbour on side i sends towards p through its opposite pipe: left <-> right, top <-> bottom
			gained += imageLoad(thermal, q)[i ^ 1];
		}
	}
	here.x += gained - (out4.x + out4.y + out4.z + out4.w);
	imageStore(dst, p, here);
}

void main()
{
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (!inside(p)) {
		return;
	}
	if (stage == STAGE_LOAD) {
		imageStore(src, p, vec4(imageLoad(heightMap, p).r * heightScale, 0.0, 0.0, 0.0));
		imageStore(flux, p, vec4(0.0));
	}
	else if (stage == STAGE_FLUX) {
		fluxStage(p);
	}
	else if (stage == STAGE_WATER) {
		waterStage(p);
	}
	else if (stage == STAGE_THERMAL_OUT) {
		thermalOutStage(p);
	}
	else if (stage == STAGE_THERMAL_APPLY) {
		thermalApplyStage(p);
	}
	else {
		// The sediment still carried settles where it is, up to the same limit as deposition (no spikes); the rest is lost with the water
		vec4 here = imageLoad(src, p);
		float highestNeighbour = max(max(terrainAt(p + ivec2(-1, 0), here.x), terrainAt(p + ivec2(1, 0), here.x)),
			max(terrainAt(p + ivec2(0, -1), here.x), terrainAt(p + ivec2(0, 1), here.x)));
		float settled = min(here.z, max(0.0, highestNeighbour + talusHeight - here.x));
		imageStore(heightMap, p, vec4((here.x + settled) / heightScale, 0.0, 0.0, 0.0));
	}
}
