#include "GpuHeightCheck.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Grid.h"
#include "GpuHeightGenerator.h"
#include "GpuSurfacePass.h"

namespace {
	//Not square, so swapped axes are detected; larger than one part of a dispatch along X
	constexpr int mapWidth = 1100, mapHeight = 257;
	//Same distance between texels as in TerrainView; a power of two, so dividing by it rounds nothing
	constexpr float surfaceTexelSize = 4.0f;

	//One line of the report, padded so that results form a column
	void Report(const std::string& name, const std::string& details, bool ok)
	{
		std::cout << std::left << std::setw(40) << name << std::setw(44) << details << (ok ? "ok" : "FAILED") << "\n";
	}

	HeightField ReadHeight(const TextureClass& texture)
	{
		HeightField map(texture.GetWidth(), texture.GetHeight());
		texture.Read(GL_RED, GL_FLOAT, static_cast<int>(map.Size() * sizeof(float)), map.Data());
		return map;
	}

	struct RangeCase {
		std::string name;
		std::function<void(TextureClass&)> generate;
		float min, max;
	};

	//Every value must be finite and within the range the configuration promises
	bool CheckRanges(GpuHeightGenerator& gpu)
	{
		std::vector<RangeCase> cases;
		const auto addNoise = [&](const std::string& name, const noise::NoiseConfigParameters& config, float min, float max) {
			cases.push_back({ "noise: " + name, [=, &gpu](TextureClass& t) { gpu.GenerateNoise(config, MapArea{}, t); }, min, max });
		};

		noise::NoiseConfigParameters n;
		n.octaves = 8;
		n.constrast = 1.5f;
		for (auto [option, name, min] : { std::tuple{ noise::Options::NOTHING, "nothing", -1.0f }, { noise::Options::REFIT_ALL, "refit", 0.0f },
			{ noise::Options::FLATTEN_NEGATIVES, "flatten", 0.0f }, { noise::Options::REVERT_NEGATIVES, "revert", 0.0f } }) {
			n.option = option;
			addNoise(std::string("option ") + name, n, min, 1.0f);
		}
		n = {}; n.Ridge = true; n.option = noise::Options::NOTHING;
		addNoise("ridge", n, 0.0f, 1.0f);
		n = {}; n.Ridge = true; n.redistribution = 0.5f; n.option = noise::Options::NOTHING;
		addNoise("ridge, redistribution 0.5", n, -1.0f, 1.0f);
		n = {}; n.redistribution = 2.5f; n.option = noise::Options::NOTHING;
		addNoise("redistribution 2.5", n, -1.0f, 1.0f);
		n = {}; n.symmetrical = true;
		addNoise("symmetrical", n, -1.0f, 1.0f);
		n = {}; n.option = noise::Options::NOTHING; n.warpStrength = 2.0f; n.warpScale = 3.0f;
		addNoise("warp", n, -1.0f, 1.0f);
		n = {}; n.option = noise::Options::NOTHING; n.slopeErosion = 2.0f;
		addNoise("slope erosion", n, -1.0f, 1.0f);
		n = {}; n.option = noise::Options::NOTHING; n.terraceSteps = 7; n.terraceSharpness = 1.0f;
		addNoise("terraces", n, -1.0f, 1.0f);
		n = {}; n.warpStrength = 1.0f; n.slopeErosion = 1.0f; n.terraceSteps = 4; n.island = true;
		addNoise("warp, erosion, terraces, island", n, 0.0f, 1.0f);
		for (auto [type, name] : { std::pair{ noise::IslandType::CONE, "cone" }, { noise::IslandType::DIAGONAL, "diagonal" },
			{ noise::IslandType::EUCLIDEAN_SQUARED, "euclidean" }, { noise::IslandType::SQUARE_BUMP, "square bump" },
			{ noise::IslandType::HYPERBOLOID, "hyperboloid" }, { noise::IslandType::SQUIRCLE, "squircle" }, { noise::IslandType::TRIG, "trig" } }) {
			n = {}; n.island = true; n.islandType = type; n.mixPower = 0.6f;
			addNoise(std::string("island ") + name, n, 0.0f, 1.0f);
		}

		TerrainConfig t = TerrainConfig::Default();
		t.method = EvaluationMethod::LINEAR_COMBINE;
		cases.push_back({ "terrain: linear", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });
		t.method = EvaluationMethod::SPLINE_COMBINE;
		cases.push_back({ "terrain: linear splines", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });
		t.continentalnessSpline.cubic = t.mountainousnessSpline.cubic = t.weirdnessSpline.cubic = true;
		//Cubic splines are monotone (SampleSpline), so they stay in [0, 1] like their points
		cases.push_back({ "terrain: cubic splines", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });
		t = TerrainConfig::Default();
		t.method = EvaluationMethod::LAYERED;
		cases.push_back({ "terrain: layered", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });
		t.mountainShare = 1.0f;
		cases.push_back({ "terrain: layered, only mountains", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });
		t.mountainShare = 0.6f;
		t.continentalnessSpline.cubic = t.mountainousnessSpline.cubic = t.peaksSpline.cubic = true;
		cases.push_back({ "terrain: layered, cubic splines", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });
		//A steep rise next to a flat part, where a natural cubic spline went far above 1
		t.continentalnessSpline = { { -1.0, -0.9, 1.0 }, { 0.0, 1.0, 1.0 }, true };
		cases.push_back({ "terrain: layered, steep cubic spline", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });

		std::cout << "values: finite and within the range of the configuration\n";
		TextureClass target(mapWidth, mapHeight, GL_R32F);
		bool allOk = true;
		for (const RangeCase& c : cases) {
			c.generate(target);
			const HeightField map = ReadHeight(target);
			size_t outside = 0;
			float low = map[0], high = map[0];
			for (size_t i = 0; i < map.Size(); i++) {
				outside += !(map[i] >= c.min && map[i] <= c.max) ? 1 : 0;
				low = std::min(low, map[i]);
				high = std::max(high, map[i]);
			}
			//A constant map means the configuration had no effect
			const bool ok = outside == 0 && high > low;
			allOk = allOk && ok;
			std::ostringstream details;
			details << std::fixed << std::setprecision(3) << "[" << low << ", " << high << "], " << outside << " outside";
			Report(c.name, details.str(), ok);
		}
		return allOk;
	}

	//The same configuration gives the same map, and a change of the seed changes it
	bool CheckDeterminism(GpuHeightGenerator& gpu)
	{
		TextureClass first(mapWidth, mapHeight, GL_R32F), second(mapWidth, mapHeight, GL_R32F);
		TerrainConfig config = TerrainConfig::Default();
		gpu.GenerateTerrain(config, MapArea{}, first);
		gpu.GenerateTerrain(config, MapArea{}, second);
		const HeightField a = ReadHeight(first), b = ReadHeight(second);
		const bool same = std::memcmp(a.Data(), b.Data(), a.Size() * sizeof(float)) == 0;
		Report("terrain: generated twice", same ? "identical" : "different", same);

		config.continentalness.seed += 1;
		gpu.GenerateTerrain(config, MapArea{}, second);
		const HeightField c = ReadHeight(second);
		size_t changed = 0;
		for (size_t i = 0; i < a.Size(); i++) {
			changed += a[i] != c[i] ? 1 : 0;
		}
		//Most texels change; some stay where a factor is 0 anyway
		const bool differs = changed > a.Size() / 2;
		Report("terrain: other continentalness seed", std::to_string(changed) + " of " + std::to_string(a.Size()) + " texels changed", differs);
		return same && differs;
	}

	//Every shape operation changes the noise, and turning it back off restores the noise bit for bit
	bool CheckShapeOperations(GpuHeightGenerator& gpu)
	{
		TextureClass target(mapWidth, mapHeight, GL_R32F);
		const noise::NoiseConfigParameters base;
		gpu.GenerateNoise(base, MapArea{}, target);
		const HeightField baseMap = ReadHeight(target);

		bool allOk = true;
		const auto check = [&](const std::string& name, const noise::NoiseConfigParameters& config) {
			gpu.GenerateNoise(config, MapArea{}, target);
			const HeightField map = ReadHeight(target);
			size_t changed = 0;
			for (size_t i = 0; i < map.Size(); i++) {
				changed += map[i] != baseMap[i] ? 1 : 0;
			}
			const bool ok = changed > map.Size() / 4;
			allOk = allOk && ok;
			Report("shape: " + name, std::to_string(changed) + " of " + std::to_string(map.Size()) + " texels changed", ok);
		};
		noise::NoiseConfigParameters n;
		n.warpStrength = 0.5f;
		check("warp", n);
		n = {}; n.slopeErosion = 0.5f;
		check("slope erosion", n);
		n = {}; n.terraceSteps = 5;
		check("terraces", n);

		//Off again: the original loop, the same bits as before the operations existed
		n = {}; n.warpScale = 3.0f; n.terraceSharpness = 0.9f;
		gpu.GenerateNoise(n, MapArea{}, target);
		const HeightField off = ReadHeight(target);
		const bool same = std::memcmp(off.Data(), baseMap.Data(), off.Size() * sizeof(float)) == 0;
		Report("shape: all off, other inactive settings", same ? "identical to the base noise" : "different", same);
		return allOk && same;
	}

	//Layered terrain: no mountains below sea level, never lower than the base on land, and mountains present inland
	bool CheckLayered(GpuHeightGenerator& gpu)
	{
		TextureClass target(mapWidth, mapHeight, GL_R32F);
		TerrainConfig t = TerrainConfig::Default();
		t.method = EvaluationMethod::LAYERED;
		const float share = t.mountainShare;
		//With no share for mountains the map is the base spline itself
		t.mountainShare = 0.0f;
		gpu.GenerateTerrain(t, MapArea{}, target);
		const HeightField base = ReadHeight(target);
		t.mountainShare = share;
		gpu.GenerateTerrain(t, MapArea{}, target);
		const HeightField map = ReadHeight(target);

		size_t sea = 0, wrongSea = 0, inland = 0, belowBase = 0, raised = 0;
		for (size_t i = 0; i < map.Size(); i++) {
			const float floor = base[i] * (1.0f - share);
			if (base[i] < t.seaLevel) {
				sea++;
				wrongSea += std::abs(map[i] - floor) > 1e-6f ? 1 : 0;
			}
			else if (base[i] >= t.seaLevel + t.coastWidth) {
				inland++;
				belowBase += map[i] < floor - 1e-6f ? 1 : 0;
				raised += map[i] > floor + 1e-3f ? 1 : 0;
			}
		}
		std::cout << "\nlayered terrain: mountains only on land\n";
		const bool seaOk = sea > 0 && wrongSea == 0;
		Report("layered: sea", std::to_string(wrongSea) + " of " + std::to_string(sea) + " texels with relief", seaOk);
		const bool landOk = inland > 0 && belowBase == 0 && raised > inland / 10;
		Report("layered: inland", std::to_string(raised) + " of " + std::to_string(inland) + " raised, " + std::to_string(belowBase) + " below base", landOk);
		return seaOk && landOk;
	}

	struct BiomeResult {
		BiomeMap ids;
		std::vector<glm::vec4> colors;
	};

	BiomeResult GenerateBiomeMaps(GpuHeightGenerator& gpu, const BiomeConfig& biomes)
	{
		TextureClass colors(mapWidth, mapHeight, GL_RGBA32F), ids(mapWidth, mapHeight, GL_R32I);
		gpu.GenerateBiomes(biomes, TerrainConfig::Default(), MapArea{}, colors, ids);
		BiomeResult result{ BiomeMap(mapWidth, mapHeight), std::vector<glm::vec4>(static_cast<size_t>(mapWidth) * mapHeight) };
		ids.Read(GL_RED_INTEGER, GL_INT, static_cast<int>(result.ids.Size() * sizeof(int)), result.ids.Data());
		colors.Read(GL_RGBA, GL_FLOAT, static_cast<int>(result.colors.size() * sizeof(glm::vec4)), result.colors.data());
		return result;
	}

	//Largest change of a color component between neighbouring texels
	float LargestColorStep(const std::vector<glm::vec4>& colors)
	{
		float largest = 0.0f;
		for (int y = 0; y < mapHeight; y++) {
			for (int x = 0; x < mapWidth; x++) {
				const glm::vec4& c = colors[static_cast<size_t>(y) * mapWidth + x];
				if (x + 1 < mapWidth) {
					const glm::vec4 d = glm::abs(colors[static_cast<size_t>(y) * mapWidth + x + 1] - c);
					largest = std::max({ largest, d.r, d.g, d.b });
				}
				if (y + 1 < mapHeight) {
					const glm::vec4 d = glm::abs(colors[static_cast<size_t>(y + 1) * mapWidth + x] - c);
					largest = std::max({ largest, d.r, d.g, d.b });
				}
			}
		}
		return largest;
	}

	//Sharp borders: every id is an index in the table and its color is the color of that biome.
	//Blending: the same ids, colors mixed only from colors of the table, and smaller color steps between texels
	bool CheckBiomes(GpuHeightGenerator& gpu)
	{
		BiomeConfig config = BiomeConfig::Default();
		const std::vector<biome::Biome>& table = config.table;
		const float blendWidth = config.blendWidth;
		config.blendWidth = 0.0f;
		const BiomeResult sharp = GenerateBiomeMaps(gpu, config);

		size_t unknown = 0, wrongColor = 0;
		std::vector<size_t> counts(table.size(), 0);
		for (size_t i = 0; i < sharp.ids.Size(); i++) {
			const int id = sharp.ids[i];
			if (id < 0 || id >= static_cast<int>(table.size())) {
				unknown++;
				continue;
			}
			counts[id]++;
			wrongColor += glm::vec3(sharp.colors[i]) != table[id].color ? 1 : 0;
		}
		const size_t present = static_cast<size_t>(std::count_if(counts.begin(), counts.end(), [](size_t c) { return c > 0; }));
		//The default world has more than one biome on a map this large
		const bool sharpOk = unknown == 0 && wrongColor == 0 && present > 1;
		Report("biomes: sharp borders", std::to_string(present) + " biomes, " + std::to_string(unknown) + " unknown ids, " + std::to_string(wrongColor) + " wrong colors", sharpOk);

		config.blendWidth = blendWidth;
		const BiomeResult blended = GenerateBiomeMaps(gpu, config);
		const bool sameIds = std::memcmp(blended.ids.Data(), sharp.ids.Data(), sharp.ids.Size() * sizeof(int)) == 0;
		glm::vec3 low(1.0f), high(0.0f);
		for (const biome::Biome& b : table) {
			low = glm::min(low, b.color);
			high = glm::max(high, b.color);
		}
		size_t outside = 0, mixed = 0;
		for (size_t i = 0; i < blended.colors.size(); i++) {
			const glm::vec3 c(blended.colors[i]);
			const bool inside = glm::all(glm::greaterThanEqual(c, low - 1e-5f)) && glm::all(glm::lessThanEqual(c, high + 1e-5f));
			outside += inside ? 0 : 1;
			mixed += c != glm::vec3(sharp.colors[i]) ? 1 : 0;
		}
		const float sharpStep = LargestColorStep(sharp.colors), blendedStep = LargestColorStep(blended.colors);
		const bool blendOk = sameIds && outside == 0 && mixed > 0 && blendedStep < sharpStep;
		std::ostringstream details;
		details << mixed << " mixed, " << outside << " outside, step " << std::fixed << std::setprecision(2) << sharpStep << " -> " << blendedStep;
		Report(std::string("biomes: blending, ids ") + (sameIds ? "same" : "DIFFERENT"), details.str(), blendOk);
		return sharpOk && blendOk;
	}

	//The climate uses the height of the terrain map: with an extreme lapse rate every texel clearly above the water is as cold
	//as it gets, and with an extreme coast humidity every texel just above the water is as humid as it gets. The chosen biome
	//has to contain that value; heights near the thresholds are skipped, the biome shader computes them on its own
	bool CheckClimate(GpuHeightGenerator& gpu)
	{
		std::cout << "\nclimate: temperature and humidity follow the terrain height\n";
		const TerrainConfig terrain = TerrainConfig::Default();
		const float water = terrain.WaterHeight();
		TextureClass heightTexture(mapWidth, mapHeight, GL_R32F);
		gpu.GenerateTerrain(terrain, MapArea{}, heightTexture);
		HeightField height(mapWidth, mapHeight);
		heightTexture.Read(GL_RED, GL_FLOAT, static_cast<int>(height.Size() * sizeof(float)), height.Data());

		BiomeConfig config = BiomeConfig::Default();
		const auto check = [&](const std::string& name, const biome::Climate& climate, auto selected, biome::Parameter parameter, float value) {
			config.climate = climate;
			const BiomeResult result = GenerateBiomeMaps(gpu, config);
			size_t texels = 0, wrong = 0;
			for (size_t i = 0; i < height.Size(); i++) {
				if (!selected(height[i] - water)) {
					continue;
				}
				texels++;
				const biome::Range& r = config.table[result.ids[i]].ranges[parameter];
				wrong += value >= r.min && value <= r.max ? 0 : 1;
			}
			const bool ok = texels > 100 && wrong == 0;
			Report(name, std::to_string(wrong) + " of " + std::to_string(texels) + " texels in a biome without " + std::to_string(value).substr(0, 4), ok);
			return ok;
		};
		const biome::Climate cold{ 1000.0f, 0.0f, 0.05f }, humid{ 0.0f, 100.0f, 0.05f };
		bool ok = check("climate: cold above the water", cold, [](float above) { return above > 0.01f; }, biome::TEMPERATURE, -1.0f);
		ok = check("climate: humid at the coast", humid, [](float above) { return above > 0.001f && above < 0.02f; }, biome::HUMIDITY, 1.0f) && ok;

		//No climate gives other biomes than the extreme one, so the climate takes part at all
		config.climate = cold;
		const BiomeResult withClimate = GenerateBiomeMaps(gpu, config);
		config.climate = biome::Climate{ 0.0f, 0.0f, 0.05f };
		const BiomeResult without = GenerateBiomeMaps(gpu, config);
		size_t different = 0;
		for (size_t i = 0; i < without.ids.Size(); i++) {
			different += without.ids[i] != withClimate.ids[i] ? 1 : 0;
		}
		const bool differs = different > 0;
		Report("climate: changes biomes", std::to_string(different) + " texels differ from no climate", differs);
		return ok && differs;
	}

	//A chunk is a part of the map generated on its own, with an origin and the island size of the whole map; a coarser
	//level of detail also takes every step-th sample. It has to equal the same samples of the whole map exactly,
	//or chunk borders would show seams and levels would not meet
	bool CheckChunks(GpuHeightGenerator& gpu)
	{
		constexpr int partWidth = 131, partHeight = 50;
		const glm::ivec2 corner(300, 30);
		bool allOk = true;
		const auto check = [&](const std::string& name, GLenum format, GLenum type, const std::function<void(const MapArea&, TextureClass&)>& generate) {
			const int bytes = format == GL_RGBA ? 16 : 4;
			const GLenum internalFormat = format == GL_RED_INTEGER ? GL_R32I : format == GL_RGBA ? GL_RGBA32F : GL_R32F;
			TextureClass whole(mapWidth, mapHeight, internalFormat);
			generate(MapArea{}, whole);
			std::vector<unsigned char> wholeData(static_cast<size_t>(mapWidth) * mapHeight * bytes), partData(static_cast<size_t>(partWidth) * partHeight * bytes);
			whole.Read(format, type, static_cast<int>(wholeData.size()), wholeData.data());
			for (int step : { 1, 2, 4 }) {
				TextureClass part(partWidth, partHeight, internalFormat);
				generate(MapArea{ glm::vec2(corner), static_cast<float>(step), glm::ivec2(mapWidth, mapHeight) }, part);
				part.Read(format, type, static_cast<int>(partData.size()), partData.data());
				int different = 0;
				for (int y = 0; y < partHeight; y++) {
					for (int x = 0; x < partWidth; x++) {
						const unsigned char* a = &wholeData[(static_cast<size_t>(corner.y + y * step) * mapWidth + corner.x + x * step) * bytes];
						const unsigned char* b = &partData[(static_cast<size_t>(y) * partWidth + x) * bytes];
						different += std::memcmp(a, b, bytes) != 0 ? 1 : 0;
					}
				}
				const bool ok = different == 0;
				allOk = allOk && ok;
				Report(name + ", step " + std::to_string(step), std::to_string(different) + " of " + std::to_string(partWidth * partHeight) + " texels differ", ok);
			}
		};

		std::cout << "\nchunks: a part of the map, also every 2nd and 4th sample, equals the whole map bit for bit\n";
		noise::NoiseConfigParameters n;
		n.island = true;
		n.mixPower = 0.6f;
		check("chunk: noise with island", GL_RED, GL_FLOAT, [&](const MapArea& area, TextureClass& t) { gpu.GenerateNoise(n, area, t); });
		noise::NoiseConfigParameters shaped;
		shaped.warpStrength = 1.0f;
		shaped.slopeErosion = 1.0f;
		shaped.terraceSteps = 4;
		check("chunk: warp, erosion, terraces", GL_RED, GL_FLOAT, [&](const MapArea& area, TextureClass& t) { gpu.GenerateNoise(shaped, area, t); });
		TerrainConfig terrain = TerrainConfig::Default();
		terrain.method = EvaluationMethod::SPLINE_COMBINE;
		check("chunk: terrain", GL_RED, GL_FLOAT, [&](const MapArea& area, TextureClass& t) { gpu.GenerateTerrain(terrain, area, t); });
		TerrainConfig layered = TerrainConfig::Default();
		layered.method = EvaluationMethod::LAYERED;
		check("chunk: layered terrain", GL_RED, GL_FLOAT, [&](const MapArea& area, TextureClass& t) { gpu.GenerateTerrain(layered, area, t); });
		const BiomeConfig biomes = BiomeConfig::Default();
		check("chunk: biome ids", GL_RED_INTEGER, GL_INT, [&](const MapArea& area, TextureClass& ids) {
			TextureClass colors(ids.GetWidth(), ids.GetHeight(), GL_RGBA32F);
			gpu.GenerateBiomes(biomes, terrain, area, colors, ids);
		});
		check("chunk: blended biome colors", GL_RGBA, GL_FLOAT, [&](const MapArea& area, TextureClass& colors) {
			TextureClass ids(colors.GetWidth(), colors.GetHeight(), GL_R32I);
			gpu.GenerateBiomes(biomes, terrain, area, colors, ids);
		});
		return allOk;
	}

	//Gradient (dx, dy) and Laplacian of h per world unit, as surface_compute.shader writes them
	struct Derivatives {
		float dx, dy, laplacian, reserved;
	};

	//Runs the surface pass on h(x, y) given in texels and checks every texel against the expected derivatives
	bool CheckSurfaceCase(GpuSurfacePass& pass, const std::string& name, const std::function<float(float, float)>& h,
		const std::function<Derivatives(int, int)>& expected)
	{
		//Small and not square; the borders are a large part of it, so one-sided differences are covered
		constexpr int width = 7, height = 5;
		HeightField input(width, height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				input.At(x, y) = h(static_cast<float>(x), static_cast<float>(y));
			}
		}
		TextureClass inputTexture(width, height, GL_R32F), output(width, height, GL_RGBA32F);
		inputTexture.Upload(input.Data(), GL_RED, GL_FLOAT);
		pass.Run(inputTexture, surfaceTexelSize, output);
		std::vector<Derivatives> result(static_cast<size_t>(width) * height);
		output.Read(GL_RGBA, GL_FLOAT, static_cast<int>(result.size() * sizeof(Derivatives)), result.data());

		float worst = 0.0f;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const Derivatives& d = result[static_cast<size_t>(y) * width + x];
				const Derivatives e = expected(x, y);
				worst = std::max({ worst, std::abs(d.dx - e.dx), std::abs(d.dy - e.dy), std::abs(d.laplacian - e.laplacian) });
				if (!std::isfinite(d.dx) || !std::isfinite(d.dy) || !std::isfinite(d.laplacian)) {
					worst = INFINITY;
				}
			}
		}
		//Inputs are small integers and halves, so the differences are exact up to a rounding or two
		const bool ok = worst <= 1e-5f;
		std::ostringstream details;
		details << "largest error " << std::scientific << std::setprecision(1) << worst;
		Report(name, details.str(), ok);
		return ok;
	}

	bool CheckSurface(GpuSurfacePass& pass)
	{
		std::cout << "\nsurface map: derivatives of maps with known derivatives\n";
		constexpr float t = surfaceTexelSize;
		bool ok = CheckSurfaceCase(pass, "surface: flat", [](float, float) { return 0.3f; },
			[](int, int) { return Derivatives{ 0.0f, 0.0f, 0.0f, 0.0f }; });
		//Rises by 0.5 per texel along x and falls by 0.25 along y; the same gradient on the borders
		ok = CheckSurfaceCase(pass, "surface: plane", [](float x, float y) { return 0.5f * x - 0.25f * y; },
			[](int, int) { return Derivatives{ 0.5f / t, -0.25f / t, 0.0f, 0.0f }; }) && ok;
		//h = x^2 + y^2: second difference 2 along each axis; inside, the central difference of x^2 is exactly 2x
		ok = CheckSurfaceCase(pass, "surface: bowl", [](float x, float y) { return x * x + y * y; },
			[](int x, int y) {
				const auto first = [](int v, int size) {
					//One-sided at the borders: (v + 1)^2 - v^2 or v^2 - (v - 1)^2
					return v == 0 ? 1.0f : v == size - 1 ? static_cast<float>(2 * v - 1) : static_cast<float>(2 * v);
				};
				return Derivatives{ first(x, 7) / t, first(y, 5) / t, 4.0f / (t * t), 0.0f };
			}) && ok;
		return ok;
	}

	//@return median time of finishing the work given by fn, in ms. The first run is skipped: drivers finish compiling shaders then
	template<typename F>
	double MedianMs(F fn, int runs)
	{
		fn();
		std::vector<double> times;
		for (int i = 0; i < runs; i++) {
			auto start = std::chrono::steady_clock::now();
			fn();
			times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
		}
		std::sort(times.begin(), times.end());
		return times[times.size() / 2];
	}

	void RunTimings(GpuHeightGenerator& gpu, GpuSurfacePass& surfacePass)
	{
		const TerrainConfig config = TerrainConfig::Default();
		const BiomeConfig biomes = BiomeConfig::Default();
		std::cout << "\nGPU time [ms]   terrain   biomes   surface map\n";
		for (int size : { 512, 1024, 2048, 4096 }) {
			TextureClass height(size, size, GL_R32F), colors(size, size, GL_RGBA32F), ids(size, size, GL_R32I), surface(size, size, GL_RGBA32F);
			const double terrainMs = MedianMs([&] {
				gpu.GenerateTerrain(config, MapArea{}, height);
				glFinish();
			}, 9);
			const double biomesMs = MedianMs([&] {
				gpu.GenerateBiomes(biomes, config, MapArea{}, colors, ids);
				glFinish();
			}, 9);
			const double surfaceMs = MedianMs([&] {
				surfacePass.Run(height, surfaceTexelSize, surface);
				glFinish();
			}, 9);
			std::cout << std::left << std::setw(14) << (std::to_string(size) + "x" + std::to_string(size)) << std::right << std::fixed << std::setprecision(2)
				<< std::setw(10) << terrainMs << std::setw(9) << biomesMs << std::setw(14) << surfaceMs << std::defaultfloat << "\n" << std::left;
		}
	}
}

int RunGpuHeightCheck()
{
	if (!glfwInit()) {
		std::cout << "[ERROR] Failed to initialize GLFW" << std::endl;
		return -1;
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	GLFWwindow* window = glfwCreateWindow(64, 64, "GPU check", nullptr, nullptr);
	if (!window) {
		std::cout << "[ERROR] OpenGL 4.5 is not available" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	int result = -1;
	if (glewInit() != GLEW_OK) {
		std::cout << "[ERROR] Failed to initialize GLEW" << std::endl;
	}
	else {
		std::cout << "GPU: " << glGetString(GL_RENDERER) << "\n\n";
		//The GL objects must go before the context does
		GpuHeightGenerator gpu;
		GpuSurfacePass surfacePass;
		if (gpu.Initialize() && surfacePass.Initialize()) {
			bool ok = CheckRanges(gpu);
			ok = CheckDeterminism(gpu) && ok;
			ok = CheckShapeOperations(gpu) && ok;
			ok = CheckLayered(gpu) && ok;
			ok = CheckBiomes(gpu) && ok;
			ok = CheckClimate(gpu) && ok;
			ok = CheckChunks(gpu) && ok;
			ok = CheckSurface(surfacePass) && ok;
			RunTimings(gpu, surfacePass);
			std::cout << "\n" << (ok ? "All checks passed" : "[ERROR] Some checks failed") << std::endl;
			result = ok ? 0 : 1;
		}
	}
	glfwDestroyWindow(window);
	glfwTerminate();
	return result;
}
