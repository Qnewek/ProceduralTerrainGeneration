#include "BiomePaint.h"

#include <algorithm>
#include <cmath>

#include "Sculpt.h"

namespace biomepaint {
	namespace {
		uint16_t Texel(int slot, int strength)
		{
			return static_cast<uint16_t>((strength << 8) | slot);
		}

		//Number in [0, 1) from a texel and the seed of a dab (integer hash, lowbias32)
		float Dither(int x, int y, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u ^ static_cast<uint32_t>(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
			h ^= h >> 16;
			h *= 0x7feb352du;
			h ^= h >> 15;
			h *= 0x846ca68bu;
			h ^= h >> 16;
			return static_cast<float>(h >> 8) / 16777216.0f;
		}
	}

	bool Layer::Empty() const
	{
		return std::none_of(texels.begin(), texels.end(), [](uint16_t t) { return (t >> 8) > 0; });
	}

	Layer Make(int width, int height)
	{
		if (width <= 0 || height <= 0) {
			return {};
		}
		Layer layer;
		layer.width = width;
		layer.height = height;
		layer.texels.assign(static_cast<size_t>(width) * height, 0);
		return layer;
	}

	uint8_t SlotOf(Layer& layer, const std::string& name)
	{
		const auto found = std::find(layer.palette.begin(), layer.palette.end(), name);
		if (found != layer.palette.end()) {
			return static_cast<uint8_t>(found - layer.palette.begin() + 1);
		}
		if (static_cast<int>(layer.palette.size()) >= maxPalette) {
			return 0;
		}
		layer.palette.push_back(name);
		return static_cast<uint8_t>(layer.palette.size());
	}

	glm::ivec4 Apply(Layer& layer, const Dab& dab)
	{
		if (layer.texels.empty() || dab.amount <= 0.0f || dab.radius <= 0.0f || (!dab.erase && dab.slot == 0)) {
			return glm::ivec4(0);
		}
		const int x0 = std::max(0, static_cast<int>(std::floor(dab.center.x - dab.radius)));
		const int y0 = std::max(0, static_cast<int>(std::floor(dab.center.y - dab.radius)));
		const int x1 = std::min(layer.width - 1, static_cast<int>(std::ceil(dab.center.x + dab.radius)));
		const int y1 = std::min(layer.height - 1, static_cast<int>(std::ceil(dab.center.y + dab.radius)));
		if (x0 > x1 || y0 > y1) {
			return glm::ivec4(0);
		}
		for (int y = y0; y <= y1; y++) {
			for (int x = x0; x <= x1; x++) {
				const float falloff = sculpt::Falloff(glm::length(glm::vec2(x, y) - dab.center), dab.radius, dab.hardness);
				//Rounded down after a random part of a step: on average the exact change, so dabs below half a step (a slow brush, many
				//frames a second, the soft edge) still add up
				const int change = static_cast<int>(std::floor(falloff * std::min(dab.amount, 1.0f) * 255.0f + Dither(x, y, dab.seed)));
				if (change <= 0) {
					continue;
				}
				uint16_t& texel = layer.texels[static_cast<size_t>(y) * layer.width + x];
				int slot = texel & 0xFF, strength = texel >> 8;
				if (dab.erase || (slot != dab.slot && strength > 0)) {
					//The eraser, or another biome fading out before the new one grows
					strength = std::max(0, strength - change);
					if (strength == 0) {
						slot = 0;
					}
				}
				else {
					slot = dab.slot;
					strength = std::min(255, strength + change);
				}
				texel = Texel(slot, strength);
			}
		}
		return glm::ivec4(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
	}

	std::array<int, 256> SlotBiomes(const Layer& layer, const std::vector<biome::Biome>& table)
	{
		std::array<int, 256> slots;
		slots.fill(-1);
		for (size_t i = 0; i < layer.palette.size() && i + 1 < slots.size(); i++) {
			const auto found = std::find_if(table.begin(), table.end(), [&](const biome::Biome& b) { return b.name == layer.palette[i]; });
			if (found != table.end()) {
				slots[i + 1] = static_cast<int>(found - table.begin());
			}
		}
		return slots;
	}

	biome::MaterialWeights PaintedMaterials(const biome::MaterialWeights& automatic, const biome::Biome& painted, float strength)
	{
		biome::MaterialWeights layers{};
		for (const biome::MaterialLayer& layer : painted.materials) {
			if (layer.material >= 0 && layer.material < biome::maxMaterials && layer.weight > 0.0f) {
				layers[layer.material] += layer.weight;
			}
		}
		float automaticSum = 0.0f, layerSum = 0.0f;
		for (int m = 0; m < biome::maxMaterials; m++) {
			automaticSum += automatic[m];
			layerSum += layers[m];
		}
		//A biome without materials adds none (as in MixMaterials), so the texel keeps the automatic ones
		if (layerSum <= 0.0f) {
			strength = 0.0f;
		}
		biome::MaterialWeights mixed{};
		for (int m = 0; m < biome::maxMaterials; m++) {
			const float a = automaticSum > 0.0f ? automatic[m] / automaticSum : 0.0f;
			const float p = layerSum > 0.0f ? layers[m] / layerSum : 0.0f;
			mixed[m] = a + (p - a) * strength;
		}
		return mixed;
	}
}
