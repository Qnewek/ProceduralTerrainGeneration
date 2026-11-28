#version 450 core

// Droplet (particle) hydraulic erosion on the GPU (H. T. Beyer, "Implementation of a method
// for hydraulic erosion"). The same order of work: every droplet makes step i before any makes step i + 1 - one dispatch of STAGE_STEP per step.
// Droplets of one step run in parallel and change the shared map at once, so the map is a fixed point buffer changed only by atomicAdd
// (no float atomics in OpenGL 4.5). The order of those changes is not fixed, so two runs with the same seed may differ slightly.
// GpuErosion runs the stages: LOAD (texture -> buffer), SPAWN, STEP x lifetime, STORE (buffer -> texture).

layout(local_size_x = 256) in;

const int STAGE_LOAD = 0;
const int STAGE_SPAWN = 1;
const int STAGE_STEP = 2;
const int STAGE_STORE = 3;

// Height = int / FIXED_SCALE: 2^-24 resolution, heights within +-127
const float FIXED_SCALE = 16777216.0;
const float FIXED_LIMIT = 127.0;

struct Droplet {
	vec2 position;
	vec2 direction;
	float velocity;
	float water;
	float sediment;
	float alive;	// 0 once the droplet left the map
};

layout(std430, binding = 0) coherent buffer HeightBuffer {
	int heights[];
};

layout(std430, binding = 1) buffer DropletBuffer {
	Droplet droplets[];
};

layout(r32f, binding = 0) uniform writeonly image2D target;
uniform sampler2D source;

uniform int stage;
uniform ivec2 mapSize;
uniform int dropletCount;	// droplets in this batch
uniform int seedBits;		// differs per batch and run; int, as the C++ side sets no uint uniforms
uniform int stepIndex;

uniform float inertia;
uniform float minSlope;
uniform float erosionRate;
uniform float depositionRate;
uniform float evaporationRate;
uniform float gravity;
uniform int erosionRadius;
uniform float blur;
uniform float initialVelocity;
uniform float initialWater;

int toFixed(float value)
{
	return int(round(clamp(value, -FIXED_LIMIT, FIXED_LIMIT) * FIXED_SCALE));
}

float heightAt(int x, int y)
{
	return float(heights[y * mapSize.x + x]) / FIXED_SCALE;
}

void addHeight(int x, int y, float amount)
{
	atomicAdd(heights[y * mapSize.x + x], toFixed(amount));
}

// PCG hash (Jarzynski, Olano: "Hash Functions for GPU Rendering")
uint pcg(uint v)
{
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

// Uniform in [0, 1), from 24 bits of the hash
float random01(uint a, uint b)
{
	return float(pcg(a ^ pcg(b + uint(seedBits) * 0x9E3779B9u)) >> 8u) / 16777216.0;
}

bool isOnMap(vec2 p)
{
	return p.x >= 0.0 && p.y >= 0.0 && p.x < float(mapSize.x) - 1.0 && p.y < float(mapSize.y) - 1.0;
}

// Same formulas as Erosion::GetGradient; (1, 1) on the last row and column, which a droplet on the map never reaches
vec2 gradientAt(vec2 p)
{
	int x = int(p.x), y = int(p.y);
	float v = p.x - float(x), u = p.y - float(y);
	if (x >= mapSize.x - 1 || y >= mapSize.y - 1) {
		return vec2(1.0);
	}
	float nw = heightAt(x, y), ne = heightAt(x + 1, y), sw = heightAt(x, y + 1), se = heightAt(x + 1, y + 1);
	return vec2((ne - nw) * (1.0 - v) + (se - sw) * v, (sw - nw) * (1.0 - u) + (se - ne) * u);
}

// Bilinear height, as Erosion::GetInterpolatedGridHeight; 0 on the last row and column
float interpolatedHeight(vec2 p)
{
	int x = int(p.x), y = int(p.y);
	if (x >= mapSize.x - 1 || y >= mapSize.y - 1) {
		return 0.0;
	}
	float v = p.x - float(x), u = p.y - float(y);
	return heightAt(x, y) * (1.0 - v) * (1.0 - u) + heightAt(x + 1, y) * v * (1.0 - u)
		+ heightAt(x, y + 1) * (1.0 - v) * u + heightAt(x + 1, y + 1) * v * u;
}

// Erosion::DistributeSediment: the four corners of the cell get the sediment by bilinear weights
void distributeSediment(vec2 p, float amount)
{
	int x = int(p.x), y = int(p.y);
	float v = p.x - float(x), u = p.y - float(y);
	addHeight(x, y, (1.0 - v) * (1.0 - u) * amount);
	if (x < mapSize.x - 1) {
		addHeight(x + 1, y, v * (1.0 - u) * amount);
	}
	if (y < mapSize.y - 1) {
		addHeight(x, y + 1, (1.0 - v) * u * amount);
		if (x < mapSize.x - 1) {
			addHeight(x + 1, y + 1, v * u * amount);
		}
	}
}

// Erosion::ErodeRadius: takes up to amount from the cells within the radius that are higher than the new position, weighted by distance.
// A cell gives at most what it has above 0 (the CPU version raised cells below 0 to 0 here).
// @return the sediment taken from the map
float erodeRadius(vec2 oldPos, vec2 newPos, float amount)
{
	int cx = int(oldPos.x), cy = int(oldPos.y);
	float radius = float(erosionRadius);
	float newHeight = heightAt(int(newPos.x), int(newPos.y));

	float weightSum = 0.0;
	for (int y = cy - erosionRadius; y < cy + erosionRadius; y++) {
		for (int x = cx - erosionRadius; x < cx + erosionRadius; x++) {
			if (x >= 0 && y >= 0 && x < mapSize.x && y < mapSize.y) {
				float distance = length(vec2(float(x), float(y)) - oldPos);
				if (distance < radius && heightAt(x, y) > newHeight) {
					weightSum += 1.0 - distance / radius;
				}
			}
		}
	}
	if (weightSum <= 0.0) {
		return 0.0;
	}

	float total = 0.0;
	for (int y = cy - erosionRadius; y < cy + erosionRadius; y++) {
		for (int x = cx - erosionRadius; x < cx + erosionRadius; x++) {
			if (x >= 0 && y >= 0 && x < mapSize.x && y < mapSize.y) {
				float distance = length(vec2(float(x), float(y)) - oldPos);
				float height = heightAt(x, y);
				if (distance < radius && height > newHeight) {
					float possible = min(amount * (1.0 - distance / radius) / weightSum, max(height, 0.0));
					// Blur keeps that part of the old height, so it is not taken either
					float taken = (1.0 - blur) * possible;
					addHeight(x, y, -taken);
					total += taken;
				}
			}
		}
	}
	return total;
}

void stepDroplet(uint index)
{
	Droplet d = droplets[index];
	if (d.alive == 0.0) {
		return;
	}

	// Droplet::AdjustDirection: blend of the old direction and the downhill gradient; on flat ground a random direction
	vec2 oldPos = d.position;
	vec2 dir = d.direction * inertia - gradientAt(oldPos) * (1.0 - inertia);
	if (dir.x == 0.0 && dir.y == 0.0) {
		uint key = index * 2u + uint(stepIndex) * 0x85EBCA6Bu;
		dir = vec2(random01(key, 7u), random01(key + 1u, 7u)) * 2.0 - 1.0;
	}
	float len = length(dir);
	if (len > 0.0) {
		dir /= len;
	}
	d.direction = dir;
	d.position = oldPos + dir;

	if (!isOnMap(d.position)) {
		d.alive = 0.0;
		droplets[index] = d;
		return;
	}

	float deltaHeight = interpolatedHeight(d.position) - interpolatedHeight(oldPos);
	if (deltaHeight >= 0.0) {
		// Uphill: fills the pit it left (Droplet::DropSediment)
		float dropped = min(deltaHeight, d.sediment);
		d.sediment -= dropped;
		distributeSediment(oldPos, dropped);
	}
	else {
		// Downhill: deposits a part of the surplus over the capacity, or erodes a part of the free capacity (Droplet::AdjustCapacity)
		float capacity = max(-deltaHeight, minSlope) * d.velocity * d.water;
		if (d.sediment > capacity) {
			float dropped = (d.sediment - capacity) * depositionRate;
			d.sediment -= dropped;
			distributeSediment(oldPos, dropped);
		}
		else {
			float toGather = min((capacity - d.sediment) * erosionRate, -deltaHeight);
			d.sediment += erodeRadius(oldPos, d.position, toGather);
		}
	}
	d.velocity = sqrt(max(0.0, d.velocity * d.velocity - deltaHeight * gravity));
	d.water *= 1.0 - evaporationRate;
	droplets[index] = d;
}

void main()
{
	// A large map needs more groups than one dispatch dimension allows, so the groups form rows
	uint index = gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x + gl_GlobalInvocationID.x;
	if (stage == STAGE_LOAD || stage == STAGE_STORE) {
		if (index >= uint(mapSize.x * mapSize.y)) {
			return;
		}
		ivec2 p = ivec2(int(index) % mapSize.x, int(index) / mapSize.x);
		if (stage == STAGE_LOAD) {
			heights[index] = toFixed(texelFetch(source, p, 0).r);
		}
		else {
			imageStore(target, p, vec4(float(heights[index]) / FIXED_SCALE, 0.0, 0.0, 0.0));
		}
		return;
	}

	if (index >= uint(dropletCount)) {
		return;
	}
	if (stage == STAGE_SPAWN) {
		// A random point of the map, as the CPU version: x in [0, width), y in [0, height)
		Droplet d;
		d.position = vec2(random01(index, 0u) * float(mapSize.x), random01(index, 1u) * float(mapSize.y));
		d.direction = vec2(0.0);
		d.velocity = initialVelocity;
		d.water = initialWater;
		d.sediment = 0.0;
		d.alive = 1.0;
		droplets[index] = d;
	}
	else {
		stepDroplet(index);
	}
}
