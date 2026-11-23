#include "PlacedPlants.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace placedplants {
	namespace {
		//splitmix64 step: well spread bits from a counter
		uint64_t Mix(uint64_t x)
		{
			x += 0x9E3779B97F4A7C15ull;
			x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
			x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
			return x ^ (x >> 31);
		}

		//Random numbers of one candidate: uniform in [0, 1)
		struct Random {
			uint64_t state;
			float Next()
			{
				state = Mix(state);
				return static_cast<float>(state >> 40) / static_cast<float>(1ull << 24);
			}
		};

		void PutFloat(std::vector<uint8_t>& out, float value)
		{
			const uint32_t bits = std::bit_cast<uint32_t>(value);
			for (int i = 0; i < 4; i++) {
				out.push_back(static_cast<uint8_t>(bits >> (8 * i)));
			}
		}

		float GetFloat(const uint8_t* data)
		{
			uint32_t bits = 0;
			for (int i = 0; i < 4; i++) {
				bits |= static_cast<uint32_t>(data[i]) << (8 * i);
			}
			return std::bit_cast<float>(bits);
		}
	}

	size_t Layer::Bytes() const
	{
		size_t bytes = plants.size() * sizeof(Plant);
		for (const std::string& name : palette) {
			bytes += name.size() + sizeof(std::string);
		}
		return bytes;
	}

	size_t Scatter(Layer& layer, const Brush& brush, glm::vec2 extent)
	{
		if (brush.models.empty() || brush.count < 1) {
			return 0;
		}
		//Palette slots of the models of the brush; a name not there yet is added
		std::vector<uint16_t> slots;
		for (const std::string& name : brush.models) {
			auto found = std::find(layer.palette.begin(), layer.palette.end(), name);
			if (found == layer.palette.end()) {
				if (static_cast<int>(layer.palette.size()) >= maxPalette) {
					return 0;
				}
				layer.palette.push_back(name);
				found = layer.palette.end() - 1;
			}
			slots.push_back(static_cast<uint16_t>(found - layer.palette.begin()));
		}
		const float spacing2 = brush.spacing * brush.spacing;
		const size_t before = layer.plants.size();
		for (int i = 0; i < brush.count; i++) {
			Random random{ (static_cast<uint64_t>(brush.seed) << 32) | static_cast<uint32_t>(i) };
			glm::vec2 position = brush.centre;
			if (brush.count > 1) {
				//Uniform over the disc
				const float r = brush.radius * std::sqrt(random.Next());
				const float angle = 2.0f * std::numbers::pi_v<float> * random.Next();
				position += r * glm::vec2(std::cos(angle), std::sin(angle));
			}
			if (position.x < 0.0f || position.y < 0.0f || position.x > extent.x || position.y > extent.y) {
				continue;
			}
			if (brush.spacing > 0.0f && std::any_of(layer.plants.begin(), layer.plants.end(), [&](const Plant& p) {
				const glm::vec2 d = p.position - position;
				return glm::dot(d, d) < spacing2;
			})) {
				continue;
			}
			const size_t choice = std::min(static_cast<size_t>(random.Next() * static_cast<float>(slots.size())), slots.size() - 1);
			const int variants = std::max(1, choice < brush.variants.size() ? brush.variants[choice] : 1);
			Plant plant;
			plant.position = position;
			plant.model = slots[choice];
			plant.variant = static_cast<uint8_t>(std::min(static_cast<int>(random.Next() * static_cast<float>(variants)), std::min(variants, 256) - 1));
			plant.yaw = 2.0f * std::numbers::pi_v<float> * random.Next();
			plant.scale = brush.scaleMin + (brush.scaleMax - brush.scaleMin) * random.Next();
			layer.plants.push_back(plant);
		}
		return layer.plants.size() - before;
	}

	size_t Erase(Layer& layer, glm::vec2 centre, float radius)
	{
		const float radius2 = radius * radius;
		const size_t before = layer.plants.size();
		std::erase_if(layer.plants, [&](const Plant& p) {
			const glm::vec2 d = p.position - centre;
			return glm::dot(d, d) <= radius2;
		});
		return before - layer.plants.size();
	}

	std::vector<vegexport::Placed> ToPlaced(const Layer& layer, const HeightField& units, float metresPerUnit, float metresPerTexel,
		const std::vector<vegexport::Model>& models)
	{
		std::vector<vegexport::Placed> placed;
		const int width = units.GetWidth(), height = units.GetHeight();
		if (width < 1 || height < 1 || !(metresPerTexel > 0.0f)) {
			return placed;
		}
		//Model of each palette slot; -1 = not in the list or not loaded
		std::vector<int> modelOf(layer.palette.size(), -1);
		for (size_t s = 0; s < layer.palette.size(); s++) {
			for (size_t m = 0; m < models.size(); m++) {
				if (models[m].name == layer.palette[s] && models[m].variants > 0) {
					modelOf[s] = static_cast<int>(m);
					break;
				}
			}
		}
		const glm::vec2 extent = glm::vec2(width - 1, height - 1) * metresPerTexel;
		placed.reserve(layer.plants.size());
		for (const Plant& plant : layer.plants) {
			const int model = plant.model < modelOf.size() ? modelOf[plant.model] : -1;
			const glm::vec2 at = plant.position;
			if (model < 0 || !(at.x >= 0.0f) || !(at.y >= 0.0f) || at.x > extent.x || at.y > extent.y) {
				continue;
			}
			//Bilinear height between the four texels around
			const glm::vec2 texel = at / metresPerTexel;
			const int x0 = std::min(static_cast<int>(texel.x), width - 1), y0 = std::min(static_cast<int>(texel.y), height - 1);
			const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
			const float fx = texel.x - static_cast<float>(x0), fy = texel.y - static_cast<float>(y0);
			const float top = units.At(x0, y0) + (units.At(x1, y0) - units.At(x0, y0)) * fx;
			const float bottom = units.At(x0, y1) + (units.At(x1, y1) - units.At(x0, y1)) * fx;
			vegexport::Placed p;
			p.position = glm::vec3(at.x, (top + (bottom - top) * fy) * metresPerUnit, at.y);
			p.yaw = plant.yaw;
			p.scale = plant.scale;
			p.model = model;
			p.variant = plant.variant % models[model].variants;
			p.layer = vegetation::LayerOfKind(models[model].kind);
			placed.push_back(p);
		}
		return placed;
	}

	std::vector<uint8_t> Pack(const Layer& layer)
	{
		std::vector<uint8_t> out;
		out.reserve(layer.plants.size() * plantBytes);
		for (const Plant& p : layer.plants) {
			PutFloat(out, p.position.x);
			PutFloat(out, p.position.y);
			PutFloat(out, p.yaw);
			PutFloat(out, p.scale);
			out.push_back(static_cast<uint8_t>(p.model));
			out.push_back(static_cast<uint8_t>(p.model >> 8));
			out.push_back(p.variant);
		}
		return out;
	}

	bool Unpack(const uint8_t* data, size_t size, Layer& layer, std::string& error)
	{
		if (size % plantBytes != 0) {
			error = "The placed plants are cut short";
			return false;
		}
		std::vector<Plant> plants(size / plantBytes);
		for (size_t i = 0; i < plants.size(); i++) {
			const uint8_t* at = data + i * plantBytes;
			Plant& p = plants[i];
			p.position = glm::vec2(GetFloat(at), GetFloat(at + 4));
			p.yaw = GetFloat(at + 8);
			p.scale = GetFloat(at + 12);
			p.model = static_cast<uint16_t>(at[16] | (at[17] << 8));
			p.variant = at[18];
			if (!std::isfinite(p.position.x) || !std::isfinite(p.position.y) || !std::isfinite(p.yaw) || !std::isfinite(p.scale) || !(p.scale > 0.0f)) {
				error = "A placed plant has a wrong position, turn or size";
				return false;
			}
			if (p.model >= layer.palette.size()) {
				error = "A placed plant names a model the project does not list";
				return false;
			}
		}
		layer.plants = std::move(plants);
		return true;
	}
}
