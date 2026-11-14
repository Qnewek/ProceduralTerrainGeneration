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

		//A plant of a biome with its model resolved
		struct ResolvedPlant {
			const biome::Plant* plant = nullptr;
			int model = 0;
		};
	}

	std::vector<uint32_t> SortByBlock(std::vector<Instance>& instances, glm::vec2 mapOrigin, float worldUnitsPerTexel, glm::ivec2 chunkTexel,
		int blockTexels, int blocksPerSide)
	{
		const size_t blocks = static_cast<size_t>(blocksPerSide) * blocksPerSide;
		std::vector<uint32_t> codes(instances.size());
		std::vector<uint32_t> starts(blocks + 1, 0);
		for (size_t i = 0; i < instances.size(); i++) {
			const glm::vec2 texel = (glm::vec2(instances[i].position.x, instances[i].position.z) - mapOrigin) / worldUnitsPerTexel - glm::vec2(chunkTexel);
			const glm::ivec2 block = glm::clamp(glm::ivec2(glm::floor(texel / static_cast<float>(blockTexels))), glm::ivec2(0), glm::ivec2(blocksPerSide - 1));
			codes[i] = Morton(static_cast<uint32_t>(block.x), static_cast<uint32_t>(block.y));
			starts[codes[i] + 1]++;
		}
		for (size_t b = 0; b < blocks; b++) {
			starts[b + 1] += starts[b];
		}
		//Counting sort, stable
		std::vector<Instance> sorted(instances.size());
		std::vector<uint32_t> next(starts.begin(), starts.end() - 1);
		for (size_t i = 0; i < instances.size(); i++) {
			sorted[next[codes[i]]++] = instances[i];
		}
		instances.swap(sorted);
		return starts;
	}

	float ClusterFactor(glm::vec2 texel, uint32_t seed)
	{
		//Two octaves, so the edges of forests are not straight
		const glm::vec2 p = texel / clusterWavelength;
		const float n = (ValueNoise(p, seed) * 2.0f + ValueNoise(p * 2.7f + 13.1f, seed + 1)) / 3.0f;
		return clusterLow + (clusterHigh - clusterLow) * n;
	}

	Layer LayerOfKind(const std::string& kind)
	{
		if (kind == "tree") {
			return Layer::TREES;
		}
		if (kind == "rock") {
			return Layer::ROCKS;
		}
		return Layer::SHRUBS;
	}

	void PlaceChunk(const ChunkMaps& maps, const std::vector<std::vector<biome::Plant>>& plants, const std::vector<ModelInfo>& models,
		const PlacementSettings& settings, std::vector<Instance>& out)
	{
		if (!maps.height || !maps.surface || !maps.ids || maps.size < 2) {
			return;
		}
		std::unordered_map<std::string, int> modelOfName;
		for (int i = 0; i < static_cast<int>(models.size()); i++) {
			modelOfName.emplace(models[i].name, i);
		}
		//Plants of every biome and layer with their models; plants of unknown models are left out, while models still loading
		//(no variants) keep their share of the cells, so the others do not move once they load
		std::vector<std::array<std::vector<ResolvedPlant>, layerCount>> plantsOf(plants.size());
		for (size_t id = 0; id < plants.size(); id++) {
			for (const biome::Plant& plant : plants[id]) {
				const auto found = modelOfName.find(plant.model);
				if (found != modelOfName.end()) {
					plantsOf[id][static_cast<int>(models[found->second].layer)].push_back({ &plant, found->second });
				}
			}
		}

		const float step = static_cast<float>(std::max(maps.step, 1));
		for (int layer = 0; layer < layerCount; layer++) {
			if (!(settings.layerMask & (1u << layer))) {
				continue;
			}
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
					const glm::vec2 local = (texel - glm::vec2(maps.firstTexel)) / step;
					const glm::ivec2 nearest = glm::clamp(glm::ivec2(glm::floor(local + 0.5f)), glm::ivec2(0), glm::ivec2(maps.size - 1));
					const size_t index = static_cast<size_t>(nearest.y) * maps.size + nearest.x;
					const int id = maps.ids[index];
					if (id < 0 || id >= static_cast<int>(plantsOf.size()) || plantsOf[id][layer].empty()) {
						continue;
					}
					const float height = SampleHeight(maps, local);
					if (height <= settings.waterHeight) {
						continue;
					}
					const float elevation = biome::Elevation(height, settings.waterHeight);
					const glm::vec4 surface = maps.surface[index];
					const float slope = std::atan(glm::length(glm::vec2(surface)) * settings.heightScale) * 180.0f / pi;

					//Pick at most one plant of this layer: each one that can grow here takes its share of [0, 1)
					float cluster = -1.0f;	//computed when a plant needs it
					const float pick = Unit(h = Hash(h));
					float sum = 0.0f;
					const ResolvedPlant* chosen = nullptr;
					for (const ResolvedPlant& candidate : plantsOf[id][layer]) {
						const biome::Plant& plant = *candidate.plant;
						if (elevation < plant.elevation.min || elevation > plant.elevation.max || slope > plant.maxSlope) {
							continue;
						}
						float factor = 1.0f;
						if (plant.clustering != 0.0f) {
							if (cluster < 0.0f) {
								cluster = ClusterFactor(texel, settings.seed);
							}
							factor = PlantCluster(cluster, plant.clustering);
						}
						sum += plant.density * settings.density * factor;
						if (pick < sum) {
							chosen = &candidate;
							break;
						}
					}
					if (!chosen || models[chosen->model].variants <= 0) {
						continue;
					}
					const biome::Plant& plant = *chosen->plant;
					Instance instance;
					instance.position = glm::vec3(settings.mapOrigin.x + texel.x * settings.worldUnitsPerTexel, height,
						settings.mapOrigin.y + texel.y * settings.worldUnitsPerTexel);
					const float size = Unit(h = Hash(h));
					instance.scale = unitsPerMetre * (plant.minScale + (plant.maxScale - plant.minScale) * size);
					instance.yaw = Unit(h = Hash(h)) * 2.0f * pi;
					instance.model = chosen->model;
					instance.variant = static_cast<int>(Hash(h) % static_cast<uint32_t>(models[chosen->model].variants));
					instance.layer = static_cast<Layer>(layer);
					out.push_back(instance);
				}
			}
		}
	}
}
