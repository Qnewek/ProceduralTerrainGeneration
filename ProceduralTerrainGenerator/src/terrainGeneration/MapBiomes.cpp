#include "MapBiomes.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <execution>
#include <numeric>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "glm/gtc/packing.hpp"

#include "Preset.h"

namespace mapbiomes {
	namespace {
		using Json = nlohmann::ordered_json;

		//Reads a number field if present; a field that is there has to be a finite number above 0 when positive is set
		void ReadFloat(const Json& root, const char* key, float& value, bool positive)
		{
			auto it = root.find(key);
			if (it == root.end()) {
				return;
			}
			if (!it->is_number()) {
				throw std::runtime_error(std::string("Map biomes: ") + key + " must be a number");
			}
			const float read = it->get<float>();
			if (!std::isfinite(read) || (positive && read <= 0.0f)) {
				throw std::runtime_error(std::string("Map biomes: ") + key + (positive ? " must be above 0" : " must be a finite number"));
			}
			value = read;
		}

		//No texel of the other kind yet (squared distance)
		constexpr float infinite = 3.0e38f;
		//The sea of a map of the real world lies within this of the sea level (ClimateHeight)
		constexpr float flatSeaMetres = 1.0f;

		//Squared distance transform along one line (Felzenszwalb and Huttenlocher): f = squared distances so far (infinite = none),
		//samples spacing apart; v, z = scratch of n and n + 1 entries
		void SquaredDistanceLine(float* line, int n, size_t stride, double spacing, std::vector<int>& v, std::vector<double>& z, std::vector<double>& f)
		{
			for (int q = 0; q < n; q++) {
				f[q] = line[q * stride];
			}
			int k = -1;
			for (int q = 0; q < n; q++) {
				if (f[q] >= infinite) {
					continue;
				}
				const double p = q * spacing;
				while (k >= 0) {
					const double r = v[k] * spacing;
					const double s = ((f[q] + p * p) - (f[v[k]] + r * r)) / (2.0 * (p - r));
					if (s > z[k]) {
						break;
					}
					k--;
				}
				k++;
				v[k] = q;
				z[k] = k == 0 ? -1e300 : ((f[q] + p * p) - (f[v[k - 1]] + (v[k - 1] * spacing) * (v[k - 1] * spacing))) / (2.0 * (p - v[k - 1] * spacing));
				z[k + 1] = 1e300;
			}
			if (k < 0) {
				return;
			}
			int j = 0;
			for (int q = 0; q < n; q++) {
				const double p = q * spacing;
				while (z[j + 1] < p) {
					j++;
				}
				const double r = v[j] * spacing;
				line[q * stride] = static_cast<float>((p - r) * (p - r) + f[v[j]]);
			}
		}

		//Runs line(i) for i in [0, count) on all cores; lines must not share data
		template<typename Line>
		void ForEachLine(int count, const Line& line)
		{
			std::vector<int> indices(static_cast<size_t>(count));
			std::iota(indices.begin(), indices.end(), 0);
			std::for_each(std::execution::par, indices.begin(), indices.end(), line);
		}

		//Columns are handled in blocks copied to contiguous memory: walking a column of a large map directly misses the cache on every texel
		constexpr int columnBlock = 16;

		//Runs column(block, x, count) on all cores for blocks of columnBlock columns, each block given as count columns of height values
		//one after another (column c of the block = map column x + c); what the function leaves in the block goes to target
		template<typename Column>
		void ForEachColumnBlock(const HeightField& source, HeightField& target, const Column& column)
		{
			const int width = source.GetWidth(), height = source.GetHeight();
			ForEachLine((width + columnBlock - 1) / columnBlock, [&](int b) {
				const int x = b * columnBlock;
				const int count = std::min(columnBlock, width - x);
				std::vector<float> block(static_cast<size_t>(count) * height);
				for (int y = 0; y < height; y++) {
					for (int c = 0; c < count; c++) {
						block[static_cast<size_t>(c) * height + y] = source.At(x + c, y);
					}
				}
				column(block.data(), count);
				for (int y = 0; y < height; y++) {
					for (int c = 0; c < count; c++) {
						target.At(x + c, y) = block[static_cast<size_t>(c) * height + y];
					}
				}
			});
		}

		//Squared Euclidean distance to the nearest texel with value 0, in metres squared; infinite where there is none
		void SquaredDistance(HeightField& squared, glm::dvec2 pixelMetres)
		{
			const int width = squared.GetWidth(), height = squared.GetHeight();
			ForEachLine(height, [&](int y) {
				std::vector<int> v(width);
				std::vector<double> z(static_cast<size_t>(width) + 1), f(width);
				SquaredDistanceLine(squared.Data() + static_cast<size_t>(y) * width, width, 1, pixelMetres.x, v, z, f);
			});
			ForEachColumnBlock(squared, squared, [&](float* block, int count) {
				std::vector<int> v(height);
				std::vector<double> z(static_cast<size_t>(height) + 1), f(height);
				for (int c = 0; c < count; c++) {
					SquaredDistanceLine(block + static_cast<size_t>(c) * height, height, 1, pixelMetres.y, v, z, f);
				}
			});
		}

		//Largest (or smallest) value within +-radius samples of every sample along a line, with a monotonic queue
		void SlidingExtreme(const float* in, float* out, int n, size_t stride, int radius, bool largest, std::vector<int>& queue)
		{
			int head = 0, tail = 0;
			int next = 0;
			for (int i = 0; i < n; i++) {
				//Takes in the samples up to i + radius
				const int last = std::min(n - 1, i + radius);
				for (; next <= last; next++) {
					const float value = in[next * stride];
					while (tail > head && (largest ? in[queue[tail - 1] * stride] <= value : in[queue[tail - 1] * stride] >= value)) {
						tail--;
					}
					queue[tail++] = next;
				}
				while (queue[head] < i - radius) {
					head++;
				}
				out[i * stride] = in[queue[head] * stride];
			}
		}
	}

	Settings Default(std::optional<float> seaLevelMetres)
	{
		Settings s;
		if (seaLevelMetres) {
			s.baseMetres = *seaLevelMetres - TerrainConfig{}.WaterHeight() * s.heightUnitMetres;
		}
		return s;
	}

	Settings FromGeneration(const std::string& generationJson, glm::ivec2 worldOrigin, float metresPerUnit, std::optional<float> seaLevelMetres,
		bool biomesOn)
	{
		WorldConfig world;
		std::string error;
		if (generationJson.empty() || !preset::FromJson(generationJson, world, error) || !(metresPerUnit > 0.0f)) {
			Settings s = Default(seaLevelMetres);
			s.climateOrigin = glm::vec2(worldOrigin);
			return s;
		}
		Settings s;
		s.enabled = biomesOn;
		s.config = world.biomes;
		s.heightUnitMetres = metresPerUnit;
		s.baseMetres = 0.0f;
		s.climateOrigin = glm::vec2(worldOrigin);
		return s;
	}

	float WaterHeight(const Settings& settings, std::optional<float> seaLevelMetres)
	{
		if (!seaLevelMetres) {
			return 0.0f;
		}
		return std::clamp((*seaLevelMetres - settings.baseMetres) / settings.heightUnitMetres, 0.0f, 1.0f);
	}

	HeightField CoastDistance(const HeightField& metres, glm::dvec2 pixelMetres, float seaLevelMetres)
	{
		const int width = metres.GetWidth(), height = metres.GetHeight();
		HeightField result(width, height);
		HeightField squared(width, height);
		for (const bool land : { true, false }) {
			//Distance of the texels on this side to the nearest texel on the other one
			for (size_t i = 0; i < metres.Size(); i++) {
				squared[i] = (metres[i] > seaLevelMetres) != land ? 0.0f : infinite;
			}
			SquaredDistance(squared, pixelMetres);
			for (size_t i = 0; i < metres.Size(); i++) {
				if ((metres[i] > seaLevelMetres) == land) {
					const float d = squared[i] >= infinite ? FLT_MAX : std::sqrt(squared[i]);
					result[i] = land ? d : -d;
				}
			}
		}
		return result;
	}

	HeightField LocalRelief(const HeightField& metres, glm::dvec2 pixelMetres, float radiusMetres)
	{
		const int width = metres.GetWidth(), height = metres.GetHeight();
		const int rx = std::max(1, static_cast<int>(std::lround(radiusMetres / pixelMetres.x)));
		const int ry = std::max(1, static_cast<int>(std::lround(radiusMetres / pixelMetres.y)));
		HeightField highest(width, height), lowest(width, height);
		//Rows, then columns of the row results
		ForEachLine(height, [&](int y) {
			std::vector<int> queue(static_cast<size_t>(width));
			const size_t row = static_cast<size_t>(y) * width;
			SlidingExtreme(metres.Data() + row, highest.Data() + row, width, 1, rx, true, queue);
			SlidingExtreme(metres.Data() + row, lowest.Data() + row, width, 1, rx, false, queue);
		});
		for (const bool largest : { true, false }) {
			HeightField& rows = largest ? highest : lowest;
			ForEachColumnBlock(rows, rows, [&](float* block, int count) {
				std::vector<int> queue(static_cast<size_t>(height));
				std::vector<float> line(static_cast<size_t>(height));
				for (int c = 0; c < count; c++) {
					float* column = block + static_cast<size_t>(c) * height;
					SlidingExtreme(column, line.data(), height, 1, ry, largest, queue);
					std::copy(line.begin(), line.end(), column);
				}
			});
		}
		//In place: a map of 8192² is 256 MB per field
		for (size_t i = 0; i < highest.Size(); i++) {
			highest[i] -= lowest[i];
		}
		return highest;
	}

	ClimateMaps ComputeClimate(const HeightField& metres, glm::dvec2 pixelMetres, std::optional<float> seaLevelMetres, const Settings& settings)
	{
		ClimateMaps maps;
		maps.width = metres.GetWidth();
		maps.height = metres.GetHeight();
		maps.packed.resize(metres.Size());
		//Relief first: its scratch fields are gone before the coast distance allocates its own
		const HeightField relief = LocalRelief(metres, pixelMetres, settings.reliefRadiusMetres);
		HeightField coast;
		if (seaLevelMetres) {
			coast = CoastDistance(metres, pixelMetres, *seaLevelMetres);
		}
		for (size_t i = 0; i < metres.Size(); i++) {
			const float c = seaLevelMetres ? std::clamp(coast[i] / settings.coastMetres, -1.0f, 1.0f) : 1.0f;
			const float m = std::clamp(2.0f * relief[i] / settings.reliefMetres - 1.0f, -1.0f, 1.0f);
			maps.packed[i] = glm::packHalf2x16(glm::vec2(c, m));
		}
		return maps;
	}

	float ClimateHeight(float metres, float continentalness, std::optional<float> seaLevelMetres, const Settings& settings)
	{
		if (seaLevelMetres && metres <= *seaLevelMetres && metres >= *seaLevelMetres - flatSeaMetres) {
			return WaterHeight(settings, seaLevelMetres) * (1.0f - std::min(std::abs(continentalness), 1.0f));
		}
		return (metres - settings.baseMetres) / settings.heightUnitMetres;
	}

	std::string ToJson(const Settings& s)
	{
		Json root;
		root["enabled"] = s.enabled;
		root["heightUnitMetres"] = s.heightUnitMetres;
		root["baseMetres"] = s.baseMetres;
		root["coastMetres"] = s.coastMetres;
		root["reliefRadiusMetres"] = s.reliefRadiusMetres;
		root["reliefMetres"] = s.reliefMetres;
		root["climateOrigin"] = { s.climateOrigin.x, s.climateOrigin.y };
		root["config"] = Json::parse(preset::BiomesJson(s.config));
		return root.dump();
	}

	bool FromJson(const std::string& text, Settings& out, std::string& error)
	{
		try {
			const Json root = Json::parse(text);
			if (!root.is_object()) {
				throw std::runtime_error("Map biomes must be a JSON object");
			}
			Settings s = out;
			if (auto it = root.find("enabled"); it != root.end()) {
				if (!it->is_boolean()) {
					throw std::runtime_error("Map biomes: enabled must be true or false");
				}
				s.enabled = it->get<bool>();
			}
			ReadFloat(root, "heightUnitMetres", s.heightUnitMetres, true);
			ReadFloat(root, "baseMetres", s.baseMetres, false);
			ReadFloat(root, "coastMetres", s.coastMetres, true);
			ReadFloat(root, "reliefRadiusMetres", s.reliefRadiusMetres, true);
			ReadFloat(root, "reliefMetres", s.reliefMetres, true);
			if (auto it = root.find("climateOrigin"); it != root.end()) {
				if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) {
					throw std::runtime_error("Map biomes: climateOrigin must be two numbers");
				}
				s.climateOrigin = glm::vec2((*it)[0].get<float>(), (*it)[1].get<float>());
				if (!std::isfinite(s.climateOrigin.x) || !std::isfinite(s.climateOrigin.y)) {
					throw std::runtime_error("Map biomes: climateOrigin must be finite");
				}
			}
			if (auto it = root.find("config"); it != root.end()) {
				std::string configError;
				if (!preset::BiomesFromJson(it->dump(), s.config, configError)) {
					throw std::runtime_error(configError);
				}
			}
			out = std::move(s);
			return true;
		}
		catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}
}
