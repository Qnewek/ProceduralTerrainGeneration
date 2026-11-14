#include "VegetationPlacement.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace vegetation
{
	namespace {
		constexpr float pi = 3.14159265358979f;
		//Wavelength of the clustering noise in texels (metres): forests and clearings of about this size
		constexpr float clusterWavelength = 96.0f;
		//Range of the clustering factor: low + (high - low) * noise, about 1 on average
		constexpr float clusterLow = 0.15f, clusterHigh = 1.85f;

		uint32_t Hash(uint32_t x)
		{
			x ^= x >> 16;
			x *= 0x7feb352dU;
			x ^= x >> 15;
			x *= 0x846ca68bU;
			x ^= x >> 16;
			return x;
		}
		uint32_t Hash(int x, int y, uint32_t salt)
		{
			return Hash(static_cast<uint32_t>(x) * 0x9E3779B1U ^ Hash(static_cast<uint32_t>(y) + 0x7F4A7C15U) ^ Hash(salt));
		}
		//Uniform in [0, 1)
		float Unit(uint32_t h)
		{
			return static_cast<float>(h >> 8) / 16777216.0f;
		}

		//Bilinear sample of the height map at a texel position of the maps (texel centers on integers)
		float SampleHeight(const ChunkMaps& maps, glm::vec2 p)
		{
			const glm::vec2 clamped = glm::clamp(p, glm::vec2(0.0f), glm::vec2(static_cast<float>(maps.size - 1)));
			const glm::ivec2 a = glm::min(glm::ivec2(glm::floor(clamped)), glm::ivec2(maps.size - 2));
			const glm::vec2 f = clamped - glm::vec2(a);
			auto at = [&](int x, int y) { return maps.height[static_cast<size_t>(y) * maps.size + x]; };
			return (at(a.x, a.y) * (1.0f - f.x) + at(a.x + 1, a.y) * f.x) * (1.0f - f.y)
				+ (at(a.x, a.y + 1) * (1.0f - f.x) + at(a.x + 1, a.y + 1) * f.x) * f.y;
		}

		//Smooth value noise in [0, 1]
		float ValueNoise(glm::vec2 p, uint32_t seed)
		{
			const glm::vec2 cell = glm::floor(p);
			const glm::vec2 f = p - cell;
			const glm::vec2 s = f * f * (3.0f - 2.0f * f);
			const int x = static_cast<int>(cell.x), y = static_cast<int>(cell.y);
			const float a = Unit(Hash(x, y, seed)), b = Unit(Hash(x + 1, y, seed));
			const float c = Unit(Hash(x, y + 1, seed)), d = Unit(Hash(x + 1, y + 1, seed));
			return (a * (1.0f - s.x) + b * s.x) * (1.0f - s.y) + (c * (1.0f - s.x) + d * s.x) * s.y;
		}
	}

	float ClusterFactor(glm::vec2 texel, uint32_t seed)
	{
		//Two octaves, so the edges of forests are not straight
		const glm::vec2 p = texel / clusterWavelength;
		const float n = (ValueNoise(p, seed) * 2.0f + ValueNoise(p * 2.7f + 13.1f, seed + 1)) / 3.0f;
		return clusterLow + (clusterHigh - clusterLow) * n;
	}

	std::vector<BiomeRules> DefaultRules()
	{
		//Probabilities per cell: trees and rocks 8 x 8 m, shrubs 4 x 4 m. Scales turn the models (sizes in vegetation_models.json)
		//into plausible sizes: the deciduous and island trees are 3 m, the saplings 1 m, the rock 0.3 m
		const auto tree = [](const char* model, float p, float minScale, float maxScale) {
			return Species{ model, Layer::TREES, p, minScale, maxScale, 32.0f };
		};
		const auto shrub = [](const char* model, float p, float minScale, float maxScale) {
			return Species{ model, Layer::SHRUBS, p, minScale, maxScale, 40.0f };
		};
		const auto rock = [](const char* model, float p, float minScale, float maxScale) {
			return Species{ model, Layer::ROCKS, p, minScale, maxScale, 70.0f };
		};
		return {
			{ "Beach", { shrub("Dead trunk", 0.004f, 0.8f, 1.4f), rock("Stone", 0.02f, 2.0f, 5.0f) } },
			{ "Stony shore", { rock("Boulder", 0.08f, 1.0f, 3.0f), rock("Rock", 0.15f, 3.0f, 8.0f), shrub("Shrub 2", 0.02f, 0.8f, 1.3f) } },
			{ "Snowy shore", { rock("Stone", 0.05f, 2.0f, 5.0f) } },
			{ "Tundra", { shrub("Pine sapling", 0.03f, 0.8f, 1.6f), shrub("Shrub 2", 0.04f, 0.8f, 1.4f), rock("Stone", 0.05f, 2.0f, 6.0f) } },
			{ "Snowfields", { tree("Fir", 0.02f, 0.6f, 0.9f), rock("Stone", 0.03f, 2.0f, 6.0f) } },
			{ "Taiga", { tree("Fir", 0.5f, 0.7f, 1.2f), shrub("Fir sapling", 0.06f, 1.0f, 2.5f), shrub("Pine sapling", 0.06f, 1.0f, 2.5f),
				shrub("Fern", 0.05f, 0.8f, 1.3f), rock("Boulder", 0.02f, 0.8f, 2.0f) } },
			{ "Steppe", { tree("Deciduous tree", 0.005f, 1.8f, 2.6f), shrub("Shrub 2", 0.05f, 0.8f, 1.5f), rock("Stone", 0.02f, 2.0f, 5.0f) } },
			{ "Plains", { tree("Deciduous tree", 0.03f, 2.0f, 3.0f), shrub("Shrub", 0.03f, 0.8f, 1.3f), shrub("Shrub 2", 0.03f, 0.8f, 1.5f),
				rock("Stone", 0.01f, 2.0f, 5.0f) } },
			{ "Forest", { tree("Deciduous tree", 0.4f, 2.0f, 3.2f), tree("Fir", 0.12f, 0.7f, 1.1f), shrub("Shrub", 0.06f, 0.8f, 1.3f),
				shrub("Fern", 0.12f, 0.8f, 1.4f), shrub("Dead trunk", 0.01f, 0.8f, 1.5f), rock("Rock", 0.02f, 3.0f, 7.0f) } },
			{ "Desert", { tree("Quiver tree", 0.01f, 2.5f, 4.0f), shrub("Dead trunk", 0.003f, 0.8f, 1.2f), rock("Stone", 0.04f, 2.0f, 6.0f),
				rock("Boulder", 0.01f, 1.0f, 3.0f) } },
			{ "Savanna", { tree("Searsia", 0.04f, 1.5f, 2.5f), tree("Island tree", 0.015f, 2.0f, 3.0f), shrub("Shrub 2", 0.05f, 0.8f, 1.5f),
				rock("Stone", 0.01f, 2.0f, 5.0f) } },
			{ "Jungle", { tree("Island tree", 0.45f, 2.2f, 3.5f), tree("Deciduous tree", 0.1f, 2.5f, 3.5f), shrub("Fern", 0.3f, 1.0f, 1.8f),
				shrub("Shrub", 0.12f, 1.0f, 1.6f) } },
			{ "Hills", { tree("Fir", 0.1f, 0.7f, 1.1f), tree("Deciduous tree", 0.08f, 2.0f, 3.0f), shrub("Shrub", 0.04f, 0.8f, 1.3f),
				rock("Boulder", 0.03f, 0.8f, 2.5f) } },
			{ "Mountain", { tree("Fir", 0.08f, 0.6f, 1.0f), rock("Boulder", 0.06f, 1.0f, 4.0f), rock("Rock", 0.05f, 4.0f, 10.0f) } },
			{ "Rocky peaks", { rock("Boulder", 0.1f, 1.5f, 5.0f), rock("Rock", 0.06f, 5.0f, 12.0f) } },
			{ "Snowy peaks", { rock("Boulder", 0.02f, 1.0f, 3.0f) } },
		};
	}

	void PlaceChunk(const ChunkMaps& maps, const std::vector<const BiomeRules*>& rules, const std::vector<ModelInfo>& models,
		const PlacementSettings& settings, std::vector<Instance>& out)
	{
		if (!maps.height || !maps.surface || !maps.ids || maps.size < 2) {
			return;
		}
		std::unordered_map<std::string, int> modelOfName;
		for (int i = 0; i < static_cast<int>(models.size()); i++) {
			modelOfName.emplace(models[i].name, i);
		}

		for (int layer = 0; layer < layerCount; layer++) {
			const int cell = cellTexels[layer];
			const int cells = settings.chunkTexels / cell;
			const uint32_t salt = settings.seed * 3u + static_cast<uint32_t>(layer) + 1u;
			for (int cy = 0; cy < cells; cy++) {
				for (int cx = 0; cx < cells; cx++) {
					//Cell in map texels; the hash of its map position makes it the same in every chunk layout
					const glm::ivec2 cellTexel = settings.chunkTexel + glm::ivec2(cx, cy) * cell;
					uint32_t h = Hash(cellTexel.x, cellTexel.y, salt);
					const float jitterX = Unit(h = Hash(h)), jitterY = Unit(h = Hash(h));
					const glm::vec2 texel = glm::vec2(cellTexel) + glm::vec2(jitterX, jitterY) * static_cast<float>(cell);
					const glm::vec2 local = texel - glm::vec2(maps.firstTexel);
					const glm::ivec2 nearest = glm::clamp(glm::ivec2(glm::floor(local + 0.5f)), glm::ivec2(0), glm::ivec2(maps.size - 1));
					const size_t index = static_cast<size_t>(nearest.y) * maps.size + nearest.x;
					const int id = maps.ids[index];
					if (id < 0 || id >= static_cast<int>(rules.size()) || !rules[id]) {
						continue;
					}
					const float height = SampleHeight(maps, local);
					if (height <= settings.waterHeight) {
						continue;
					}
					//Pick at most one species of this layer: each takes its share of [0, 1)
					const float cluster = layer == static_cast<int>(Layer::ROCKS) ? 1.0f : ClusterFactor(texel, settings.seed);
					const float pick = Unit(h = Hash(h));
					float sum = 0.0f;
					const Species* chosen = nullptr;
					for (const Species& species : rules[id]->species) {
						if (static_cast<int>(species.layer) != layer) {
							continue;
						}
						sum += species.probability * settings.density * cluster;
						if (pick < sum) {
							chosen = &species;
							break;
						}
					}
					if (!chosen) {
						continue;
					}
					const auto found = modelOfName.find(chosen->model);
					if (found == modelOfName.end() || models[found->second].variants <= 0) {
						continue;
					}
					const glm::vec4 surface = maps.surface[index];
					const float slope = std::atan(glm::length(glm::vec2(surface)) * settings.heightScale) * 180.0f / pi;
					if (slope > chosen->maxSlope) {
						continue;
					}
					Instance instance;
					instance.position = glm::vec3(settings.mapOrigin.x + texel.x * settings.worldUnitsPerTexel, height,
						settings.mapOrigin.y + texel.y * settings.worldUnitsPerTexel);
					const float size = Unit(h = Hash(h));
					instance.scale = unitsPerMetre * (chosen->minScale + (chosen->maxScale - chosen->minScale) * size);
					instance.yaw = Unit(h = Hash(h)) * 2.0f * pi;
					instance.model = found->second;
					instance.variant = static_cast<int>(Hash(h) % static_cast<uint32_t>(models[found->second].variants));
					instance.layer = static_cast<Layer>(layer);
					out.push_back(instance);
				}
			}
		}
	}
}
