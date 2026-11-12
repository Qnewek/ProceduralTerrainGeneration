// Biome rules shared by biome_compute.shader and height_compute.shader (terrain modifiers); the same as Biome.cpp:
// biomeDistance = biome::Biome::Distance, applyClimate = biome::ApplyClimate, blendWeight = biome::BlendWeight,
// modifyHeight = biome::ModifyHeight, the material sums in biome_compute.shader + finishMaterials = biome::MixMaterials
// (biome_compute.shader also repeats the sums of modifyHeight in its single loop over the biomes).
// A change here needs the same change there (and the other way round).
// Needs noise.glsl included first (simplex2, permutation table 5 = detail noise, also used by the material patches).

const int PARAMETER_COUNT = 5;
const int DETAIL_TABLE = 5;
const int MATERIAL_LAYERS = 4;		// biome::materialLayers
const int MAX_MATERIALS = 16;		// biome::maxMaterials

// Matches GpuBiomeEntry in GpuHeightGenerator.cpp
struct BiomeEntry {
	float ranges[2 * PARAMETER_COUNT];	// min and max of every parameter, in the order of biome::Parameter
	float r, g, b;
	float amplitude, offset, detailAmplitude, detailSize;	// biome::TerrainModifier
	int detailRidge;
	int materials[MATERIAL_LAYERS];			// biome::MaterialLayer
	float materialWeights[MATERIAL_LAYERS];	// normalized to sum 1 on upload; 0 = unused layer
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

// Material patches (BiomeConfig::materialPatchiness, materialPatchSize)
uniform float materialBlendWidth;	// > 0
uniform float materialPatchiness;	// [0, 1]
uniform float materialPatchContrast;	// >= 1
uniform float materialPatchSize;	// wavelength in map texels, > 0
const float PATCH_NOISE_SCALE = 5.0;	// biome::patchNoiseScale

// Patch noise of a material: 2 octaves of simplex noise in [-1, 1], shifted per material so materials form different patches
float patchNoise(int material, vec2 position)
{
	vec2 p = position / materialPatchSize + vec2(float(material) * 37.17, float(material) * -51.63);
	return (simplex2(DETAIL_TABLE, p.x, p.y) + 0.5 * simplex2(DETAIL_TABLE, p.x * 2.0, p.y * 2.0)) / 1.5;
}

// Second step of biome::MixMaterials: patches on the mix of the biomes (weights = sums of the material layers weighted by
// blendWeight with materialBlendWidth); the result sums to 1
void finishMaterials(inout float weights[MAX_MATERIALS], vec2 position)
{
	// Softmax of c * (log w + k * patch) in the log space: the largest score is subtracted, so exp cannot overflow
	float k = PATCH_NOISE_SCALE * materialPatchiness;
	float c = 1.0 + (materialPatchContrast - 1.0) * materialPatchiness;
	float score[MAX_MATERIALS];
	float best = -3.4e38;
	for (int m = 0; m < MAX_MATERIALS; m++) {
		// The noise only for materials that are there
		if (weights[m] > 0.0) {
			score[m] = c * (log(weights[m]) + k * patchNoise(m, position));
			best = max(best, score[m]);
		}
	}
	float sum = 0.0;
	for (int m = 0; m < MAX_MATERIALS; m++) {
		weights[m] = weights[m] > 0.0 ? exp(score[m] - best) : 0.0;
		sum += weights[m];
	}
	// sum >= 1: the best material has exp(0)
	for (int m = 0; m < MAX_MATERIALS; m++) {
		weights[m] /= sum;
	}
}

// Last step of modifyHeight from the sums over the biomes, each term weighted by blendWeight with terrainBlendWidth;
// total >= 1, because the nearest biome has weight 1
float finishModifiedHeight(float height, float total, float amplitude, float offset, float details)
{
	return clamp(waterHeight + (height - waterHeight) * amplitude / total + (offset + details) / total, 0.0, 1.0);
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
	return finishModifiedHeight(height, total, amplitude, offset, details);
}
