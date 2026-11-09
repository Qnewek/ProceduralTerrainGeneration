// Biome rules shared by biome_compute.shader and height_compute.shader (terrain modifiers); the same as Biome.cpp:
// biomeDistance = biome::Biome::Distance, applyClimate = biome::ApplyClimate, blendWeight = biome::BlendWeight,
// modifyHeight = biome::ModifyHeight. A change here needs the same change there (and the other way round).
// Needs noise.glsl included first (simplex2, permutation table 5 = detail noise).

const int PARAMETER_COUNT = 5;
const int DETAIL_TABLE = 5;

// Matches GpuBiomeEntry in GpuHeightGenerator.cpp
struct BiomeEntry {
	float ranges[2 * PARAMETER_COUNT];	// min and max of every parameter, in the order of biome::Parameter
	float r, g, b;
	float amplitude, offset, detailAmplitude, detailSize;	// biome::TerrainModifier
	int detailRidge;
};

layout(std430, binding = 1) readonly buffer BiomeTable {
	BiomeEntry biomes[];
};

uniform int biomeCount;
uniform float waterHeight;			// TerrainConfig::WaterHeight
uniform vec3 climate;				// lapseRate, coastHumidity, coastRange (biome::Climate)
uniform float terrainBlendWidth;	// BiomeConfig::terrainBlendWidth, > 0

float biomeDistance(int b, float values[PARAMETER_COUNT])
{
	float sum = 0.0;
	for (int i = 0; i < PARAMETER_COUNT; i++) {
		float outside = max(max(biomes[b].ranges[i * 2] - values[i], values[i] - biomes[b].ranges[i * 2 + 1]), 0.0);
		sum += outside * outside;
	}
	return sqrt(sum);
}

void applyClimate(inout float values[PARAMETER_COUNT], float height)
{
	float above = max(height - waterHeight, 0.0);
	float coast = 1.0 - smoothstep(0.0, climate.z, above);
	values[3] = clamp(values[3] - climate.x * above, -1.0, 1.0);
	values[4] = clamp(values[4] + climate.y * coast, -1.0, 1.0);
}

// Id of the nearest biome, the first one on ties
int nearestBiome(float values[PARAMETER_COUNT], out float nearest)
{
	int id = 0;
	nearest = biomeDistance(0, values);
	for (int b = 1; b < biomeCount; b++) {
		float distance = biomeDistance(b, values);
		if (distance < nearest) {
			nearest = distance;
			id = b;
		}
	}
	return id;
}

// width > 0
float blendWeight(float distance, float nearest, float width)
{
	return 1.0 - smoothstep(0.0, 1.0, (distance - nearest) / width);
}

// 3 octaves of simplex noise with the wavelength detailSize (map texels), in [-1, 1]; ridged = 1 - 2|n|
float detailNoise(int b, vec2 position)
{
	vec2 p = position / biomes[b].detailSize;
	float sum = 0.0, amplitude = 1.0, frequency = 1.0, divider = 0.0;
	for (int i = 0; i < 3; i++) {
		sum += simplex2(DETAIL_TABLE, p.x * frequency, p.y * frequency) * amplitude;
		divider += amplitude;
		amplitude *= 0.5;
		frequency *= 2.0;
	}
	float n = sum / divider;
	return biomes[b].detailRidge != 0 ? 1.0 - 2.0 * abs(n) : n;
}

// Height changed by the modifiers of the biomes chosen from values (after applyClimate); position = map position
float modifyHeight(float values[PARAMETER_COUNT], float height, vec2 position)
{
	float nearest;
	nearestBiome(values, nearest);
	float total = 0.0, amplitude = 0.0, offset = 0.0, details = 0.0;
	for (int b = 0; b < biomeCount; b++) {
		float weight = blendWeight(biomeDistance(b, values), nearest, terrainBlendWidth);
		if (weight <= 0.0) {
			continue;
		}
		total += weight;
		amplitude += weight * biomes[b].amplitude;
		offset += weight * biomes[b].offset;
		// The noise only where it is seen
		if (biomes[b].detailAmplitude != 0.0) {
			details += weight * biomes[b].detailAmplitude * detailNoise(b, position);
		}
	}
	// total >= 1: the nearest biome has weight 1
	return clamp(waterHeight + (height - waterHeight) * amplitude / total + (offset + details) / total, 0.0, 1.0);
}
