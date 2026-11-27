#include "GpuHeightCheck.h"

#include <algorithm>
#include <array>
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

#include "AsyncRead.h"
#include "BiomePaint.h"
#include "Grid.h"
#include "GpuErosion.h"
#include "GpuHeightGenerator.h"
#include "GpuSculpt.h"
#include "GpuSurfacePass.h"
#include "Hydrology.h"
#include "LandStats.h"
#include "MapBiomes.h"
#include "NestedSpline.h"
#include "glm/gtc/packing.hpp"
#include "RiverMap.h"
#include "TerrainView.h"

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

	//Every curve of the tree smooth (cubic) or straight
	void SetCubic(NestedCurve& curve, bool cubic)
	{
		curve.cubic = cubic;
		for (NestedPoint& p : curve.points) {
			for (NestedCurve& sub : p.sub) {
				SetCubic(sub, cubic);
			}
		}
	}

	//NESTED with the tree made from the default LAYERED curves, every curve cubic if asked
	TerrainConfig NestedFromLayered(bool cubic)
	{
		TerrainConfig t = TerrainConfig::Default();
		t.method = EvaluationMethod::NESTED;
		t.nested = nested::FromLayered(t);
		if (cubic) {
			SetCubic(t.nested.root, true);
		}
		return t;
	}

	//Constants and curves on every level, straight and cubic curves, two W-curves shared by several points
	NestedCurve HandBuiltTree()
	{
		const auto constants = [](std::vector<std::pair<double, double>> points, bool cubic) {
			NestedCurve c;
			c.cubic = cubic;
			for (auto [x, y] : points) {
				c.points.push_back({ x, y, {} });
			}
			return c;
		};
		const NestedCurve w1 = constants({ { -1.0, 0.1 }, { 0.0, 0.5 }, { 1.0, 0.9 } }, true);
		const NestedCurve w2 = constants({ { -1.0, 0.9 }, { -0.3, 0.2 }, { 0.4, 0.6 }, { 1.0, 0.3 } }, false);
		NestedCurve m1 = constants({ { -1.0, 0.2 }, { 0.0, 0.0 }, { 0.5, 0.7 }, { 1.0, 0.0 } }, true);
		m1.points[1].sub = { w1 };
		m1.points[3].sub = { w2 };
		NestedCurve m2 = constants({ { -1.0, 0.05 }, { 0.2, 0.0 }, { 1.0, 0.4 } }, false);
		m2.points[1].sub = { w2 };
		NestedCurve root = constants({ { -1.0, 0.0 }, { -0.2, 0.0 }, { 0.3, 0.0 }, { 1.0, 0.95 } }, true);
		root.points[1].sub = { m2 };
		root.points[2].sub = { m1 };
		return root;
	}

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
		t = NestedFromLayered(false);
		cases.push_back({ "terrain: nested (from layered)", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });
		//Cubic curves are monotone, so the tree stays in [0, 1] like its values
		t = NestedFromLayered(true);
		cases.push_back({ "terrain: nested, cubic", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });

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

	//The nested tree on the GPU (terrain.glsl) gives the heights of nested::Evaluate at the noise values of each texel
	bool CheckNested(GpuHeightGenerator& gpu)
	{
		std::cout << "\nnested splines: the GPU equals the CPU rule (nested::Evaluate)\n";
		TerrainConfig handBuilt = NestedFromLayered(false);
		handBuilt.nested.root = HandBuiltTree();
		const std::pair<std::string, TerrainConfig> cases[] = {
			{ "nested: GPU equals CPU, from layered", NestedFromLayered(false) },
			{ "nested: GPU equals CPU, cubic", NestedFromLayered(true) },
			{ "nested: GPU equals CPU, hand-built", handBuilt },
		};
		TextureClass c(mapWidth, mapHeight, GL_R32F), m(mapWidth, mapHeight, GL_R32F), w(mapWidth, mapHeight, GL_R32F), height(mapWidth, mapHeight, GL_R32F);
		bool allOk = true;
		for (const auto& [name, t] : cases) {
			//The same noises as the terrain evaluates (evaluateNoise with the parameters and seed of each)
			gpu.GenerateNoise(t.continentalness, MapArea{}, c);
			gpu.GenerateNoise(t.mountainousness, MapArea{}, m);
			gpu.GenerateNoise(t.weirdness, MapArea{}, w);
			gpu.GenerateTerrain(t, MapArea{}, height);
			const HeightField cm = ReadHeight(c), mm = ReadHeight(m), wm = ReadHeight(w), hm = ReadHeight(height);
			double largest = 0.0;
			float low = hm[0], high = hm[0];
			for (size_t i = 0; i < hm.Size(); i++) {
				const double expected = nested::Evaluate(t.nested.root, cm[i], mm[i], wm[i]);
				const double difference = std::abs(expected - hm[i]);
				largest = std::isfinite(difference) ? std::max(largest, difference) : 1e30;
				low = std::min(low, hm[i]);
				high = std::max(high, hm[i]);
			}
			const bool ok = largest <= 1e-5 && high > low;
			allOk = allOk && ok;
			std::ostringstream details;
			details << "largest difference " << std::scientific << std::setprecision(2) << largest;
			Report(name, details.str(), ok);
		}
		return allOk;
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

	//Largest height difference between neighbouring texels
	float LargestHeightStep(const HeightField& map)
	{
		float largest = 0.0f;
		for (int y = 0; y < map.GetHeight(); y++) {
			for (int x = 0; x < map.GetWidth(); x++) {
				if (x + 1 < map.GetWidth()) {
					largest = std::max(largest, std::abs(map.At(x + 1, y) - map.At(x, y)));
				}
				if (y + 1 < map.GetHeight()) {
					largest = std::max(largest, std::abs(map.At(x, y + 1) - map.At(x, y)));
				}
			}
		}
		return largest;
	}

	//Biome modifiers: tables with known results (neutral, flat, raised), then the default table: values in [0, 1], the terrain
	//changed, and blending the modifiers makes the steps between texels smaller than switching them at the borders
	bool CheckModifiers(GpuHeightGenerator& gpu)
	{
		std::cout << "\nbiome modifiers: terrain changed by the biomes\n";
		const TerrainConfig terrain = TerrainConfig::Default();
		const float water = terrain.WaterHeight();
		TextureClass target(mapWidth, mapHeight, GL_R32F);
		gpu.GenerateTerrain(terrain, MapArea{}, target);
		const HeightField base = ReadHeight(target);
		const auto generate = [&](const BiomeConfig& biomes) {
			gpu.GenerateTerrain(terrain, MapArea{}, target, &biomes);
			return ReadHeight(target);
		};
		//Largest difference from expected(base height)
		const auto largestError = [&](const HeightField& map, const std::function<float(float)>& expected) {
			float worst = 0.0f;
			for (size_t i = 0; i < map.Size(); i++) {
				worst = std::isfinite(map[i]) ? std::max(worst, std::abs(map[i] - expected(base[i]))) : INFINITY;
			}
			return worst;
		};
		const auto report = [](const std::string& name, float error) {
			std::ostringstream details;
			details << "largest error " << std::scientific << std::setprecision(1) << error;
			//Mixing equal modifiers divides and multiplies by the sum of weights, so a rounding or two
			const bool ok = error <= 1e-5f;
			Report(name, details.str(), ok);
			return ok;
		};

		BiomeConfig biomes = BiomeConfig::Default();
		const std::vector<biome::Biome> defaults = biomes.table;
		const auto setAll = [&](const biome::TerrainModifier& m) {
			for (biome::Biome& b : biomes.table) {
				b.terrain = m;
			}
		};
		setAll(biome::TerrainModifier{});
		bool ok = report("modifiers: neutral keep the terrain", largestError(generate(biomes), [](float h) { return h; }));
		setAll(biome::TerrainModifier{ 0.0f, 0.0f, 0.0f, 16.0f, false });
		ok = report("modifiers: amplitude 0 = water level", largestError(generate(biomes), [&](float) { return water; })) && ok;
		setAll(biome::TerrainModifier{ 1.0f, 0.1f, 0.0f, 16.0f, false });
		ok = report("modifiers: offset raises", largestError(generate(biomes), [](float h) { return std::min(h + 0.1f, 1.0f); })) && ok;
		biomes.modifyTerrain = false;
		ok = report("modifiers: turned off", largestError(generate(biomes), [](float h) { return h; })) && ok;
		biomes.modifyTerrain = true;

		//The default table without the detail noise, so only the mixing of amplitudes and offsets makes steps
		biomes.table = defaults;
		for (biome::Biome& b : biomes.table) {
			b.terrain.detailAmplitude = 0.0f;
		}
		const float blendWidth = biomes.terrainBlendWidth;
		biomes.terrainBlendWidth = 1e-4f;
		const float sharpStep = LargestHeightStep(generate(biomes));
		biomes.terrainBlendWidth = blendWidth;
		const float blendedStep = LargestHeightStep(generate(biomes));
		std::ostringstream blendDetails;
		blendDetails << "step " << std::fixed << std::setprecision(4) << sharpStep << " -> " << blendedStep;
		const bool blendOk = blendedStep < sharpStep;
		Report("modifiers: blending smooths borders", blendDetails.str(), blendOk);

		biomes.table = defaults;
		const HeightField modified = generate(biomes);
		size_t outside = 0, changed = 0;
		for (size_t i = 0; i < modified.Size(); i++) {
			outside += std::isfinite(modified[i]) && modified[i] >= 0.0f && modified[i] <= 1.0f ? 0 : 1;
			changed += std::abs(modified[i] - base[i]) > 1e-4f ? 1 : 0;
		}
		const bool defaultOk = outside == 0 && changed > modified.Size() / 10;
		Report("modifiers: default table", std::to_string(changed) + " texels changed, " + std::to_string(outside) + " outside [0, 1]", defaultOk);
		return ok && blendOk && defaultOk;
	}

	//Splat map of the default terrain: weights (0-255) of every material, texel after texel
	std::vector<std::array<int, biome::maxMaterials>> GenerateSplat(GpuHeightGenerator& gpu, const BiomeConfig& biomes, const MapArea& area,
		int width, int height)
	{
		TextureClass colors(width, height, GL_RGBA32F), ids(width, height, GL_R32I);
		TextureClass splat(width, height, GL_RGBA8, GpuHeightGenerator::splatLayers);
		gpu.GenerateBiomes(biomes, TerrainConfig::Default(), area, colors, ids, &splat);
		const size_t texels = static_cast<size_t>(width) * height;
		std::vector<unsigned char> data(texels * 4 * GpuHeightGenerator::splatLayers);
		splat.Read(GL_RGBA, GL_UNSIGNED_BYTE, static_cast<int>(data.size()), data.data());
		std::vector<std::array<int, biome::maxMaterials>> weights(texels);
		for (int layer = 0; layer < GpuHeightGenerator::splatLayers; layer++) {
			for (size_t i = 0; i < texels; i++) {
				for (int c = 0; c < 4; c++) {
					weights[i][layer * 4 + c] = data[(layer * texels + i) * 4 + c];
				}
			}
		}
		return weights;
	}

	//Largest change of a material weight (0-255) between neighbouring texels
	int LargestWeightStep(const std::vector<std::array<int, biome::maxMaterials>>& weights)
	{
		int largest = 0;
		for (int y = 0; y < mapHeight; y++) {
			for (int x = 0; x < mapWidth; x++) {
				const auto& w = weights[static_cast<size_t>(y) * mapWidth + x];
				for (int m = 0; m < biome::maxMaterials; m++) {
					if (x + 1 < mapWidth) {
						largest = std::max(largest, std::abs(weights[static_cast<size_t>(y) * mapWidth + x + 1][m] - w[m]));
					}
					if (y + 1 < mapHeight) {
						largest = std::max(largest, std::abs(weights[static_cast<size_t>(y + 1) * mapWidth + x][m] - w[m]));
					}
				}
			}
		}
		return largest;
	}

	//Splat map (biome::MixMaterials): weights sum to 1 (up to 8-bit rounding), only materials of the table appear,
	//a single material covers everything, patches change the mix, and blending makes the steps between texels smaller
	bool CheckMaterials(GpuHeightGenerator& gpu)
	{
		std::cout << "\nmaterials: splat map from the biomes\n";
		const BiomeConfig config = BiomeConfig::Default();
		const auto weights = GenerateSplat(gpu, config, MapArea{}, mapWidth, mapHeight);
		std::array<bool, biome::maxMaterials> used{};
		for (const biome::Biome& b : config.table) {
			for (const biome::MaterialLayer& layer : b.materials) {
				used[layer.material] = used[layer.material] || layer.weight > 0.0f;
			}
		}
		//Each of the 16 weights rounds by at most half a step
		size_t badSum = 0, unused = 0;
		std::array<size_t, biome::maxMaterials> present{};
		for (const auto& w : weights) {
			int sum = 0;
			for (int m = 0; m < biome::maxMaterials; m++) {
				sum += w[m];
				unused += !used[m] && w[m] > 0 ? 1 : 0;
				present[m] += w[m] > 0 ? 1 : 0;
			}
			badSum += std::abs(sum - 255) > 8 ? 1 : 0;
		}
		//For tuning: on how many texels each material has the largest share, and its mean share
		std::array<size_t, biome::maxMaterials> dominant{};
		std::array<double, biome::maxMaterials> shareSum{};
		for (const auto& w : weights) {
			dominant[std::max_element(w.begin(), w.end()) - w.begin()]++;
			for (int m = 0; m < biome::maxMaterials; m++) {
				shareSum[m] += w[m] / 255.0;
			}
		}
		//Transitions: texels where no material has 90% of the mix, for several patch contrasts
		std::cout << "  texels in a transition (no material >= 90%):";
		for (float contrast : { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f }) {
			BiomeConfig c = config;
			c.materialPatchContrast = contrast;
			const auto w = GenerateSplat(gpu, c, MapArea{}, mapWidth, mapHeight);
			const size_t transition = static_cast<size_t>(std::count_if(w.begin(), w.end(), [](const auto& t) { return *std::max_element(t.begin(), t.end()) < 230; }));
			std::cout << "  contrast " << contrast << ": " << std::fixed << std::setprecision(1) << 100.0 * transition / w.size() << "%";
		}
		std::cout << "\n  material: dominant on % of texels, mean share %\n";
		for (int m = 0; m < biome::maxMaterials; m++) {
			if (present[m] > 0) {
				std::cout << "  " << std::setw(2) << m << ": " << std::fixed << std::setprecision(1) << std::setw(5) << 100.0 * dominant[m] / weights.size()
					<< "  " << std::setw(5) << 100.0 * shareSum[m] / weights.size() << "\n";
			}
		}
		const size_t materialsPresent = static_cast<size_t>(std::count_if(present.begin(), present.end(), [](size_t c) { return c > 0; }));
		const bool sumOk = badSum == 0 && unused == 0 && materialsPresent > 1;
		Report("materials: default table", std::to_string(materialsPresent) + " materials, " + std::to_string(badSum) + " bad sums, "
			+ std::to_string(unused) + " unused materials", sumOk);

		BiomeConfig single = config;
		for (biome::Biome& b : single.table) {
			b.materials = { { { 3, 2.0f } } };
		}
		const auto singleWeights = GenerateSplat(gpu, single, MapArea{}, mapWidth, mapHeight);
		const size_t notFull = static_cast<size_t>(std::count_if(singleWeights.begin(), singleWeights.end(), [](const auto& w) { return w[3] != 255; }));
		const bool singleOk = notFull == 0;
		Report("materials: one material everywhere", std::to_string(notFull) + " texels not fully material 3", singleOk);

		BiomeConfig even = config;
		even.materialPatchiness = 0.0f;
		const auto evenWeights = GenerateSplat(gpu, even, MapArea{}, mapWidth, mapHeight);
		size_t patched = 0;
		for (size_t i = 0; i < weights.size(); i++) {
			patched += weights[i] != evenWeights[i] ? 1 : 0;
		}
		const bool patchOk = patched > weights.size() / 10;
		Report("materials: patches change the mix", std::to_string(patched) + " of " + std::to_string(weights.size()) + " texels", patchOk);

		BiomeConfig sharp = even;
		sharp.materialBlendWidth = 0.001f;
		const int sharpStep = LargestWeightStep(GenerateSplat(gpu, sharp, MapArea{}, mapWidth, mapHeight)), evenStep = LargestWeightStep(evenWeights);
		const bool blendOk = evenStep < sharpStep;
		Report("materials: blending", "largest step " + std::to_string(sharpStep) + " -> " + std::to_string(evenStep), blendOk);
		return sumOk && singleOk && patchOk && blendOk;
	}

	//Water and landmasses of the default world, sampled as the Terrain tab does; the defaults are calibrated to about 60% water
	//in a few large landmasses, so a change of the default terrain that breaks it shows here
	bool CheckLand(GpuHeightGenerator& gpu)
	{
		std::cout << "\nland: water and landmasses of the default world (" << land::sampleSize << "^2 samples, every " << land::sampleStep << " texels)\n";
		const TerrainConfig terrain = TerrainConfig::Default();
		const BiomeConfig biomes = BiomeConfig::Default();
		bool allOk = true;
		for (const auto& [name, withBiomes] : { std::pair{ "land: default world", false }, { "land: default world, biome modifiers", true } }) {
			const HeightField map = gpu.SampleTerrain(terrain, glm::vec2(0.0f), land::sampleSize, land::sampleStep, glm::ivec2(mapWidth, mapHeight),
				withBiomes ? &biomes : nullptr);
			const LandStats s = land::Measure(map, terrain.WaterHeight());
			std::ostringstream details;
			details << std::fixed << std::setprecision(1) << 100.0f * s.waterShare << "% water, " << s.landmasses << " masses, "
				<< 100.0f * s.continentalShare << "% continental ";
			const bool ok = s.waterShare >= 0.55f && s.waterShare <= 0.65f && s.continentalShare >= 0.8f;
			allOk = allOk && ok;
			Report(name, details.str(), ok);
		}
		return allOk;
	}

	//All texels of a texture as bytes (every layer of an array)
	std::vector<unsigned char> ReadBytes(const TextureClass& texture, GLenum format, GLenum type, int bytesPerTexel)
	{
		std::vector<unsigned char> data(static_cast<size_t>(texture.GetWidth()) * texture.GetHeight() * texture.GetLayers() * bytesPerTexel);
		texture.Read(format, type, static_cast<int>(data.size()), data.data());
		return data;
	}

	//GenerateWorld (one pass) gives the same height, colors, ids and splat map as GenerateTerrain with the biome modifiers
	//followed by GenerateBiomes, bit for bit, also for a chunk with a coarser step
	bool CheckWorld(GpuHeightGenerator& gpu)
	{
		std::cout << "\nworld: terrain and biomes in one pass equal the two passes bit for bit\n";
		struct WorldCase {
			std::string name;
			TerrainConfig terrain;
			BiomeConfig biomes;
			MapArea area;
		};
		std::vector<WorldCase> cases;
		cases.push_back({ "world: default", TerrainConfig::Default(), BiomeConfig::Default(), MapArea{} });
		BiomeConfig unmodified = BiomeConfig::Default();
		unmodified.modifyTerrain = false;
		cases.push_back({ "world: without modifiers", TerrainConfig::Default(), unmodified, MapArea{} });
		TerrainConfig spline = TerrainConfig::Default();
		spline.method = EvaluationMethod::SPLINE_COMBINE;
		BiomeConfig sharp = BiomeConfig::Default();
		sharp.blendWidth = 0.0f;
		cases.push_back({ "world: spline method, sharp colors", spline, sharp, MapArea{} });
		cases.push_back({ "world: chunk, step 2", TerrainConfig::Default(), BiomeConfig::Default(),
			MapArea{ glm::vec2(300.0f, 30.0f), 2.0f, glm::ivec2(mapWidth, mapHeight) } });
		cases.push_back({ "world: nested method", NestedFromLayered(true), BiomeConfig::Default(), MapArea{} });

		bool allOk = true;
		for (const WorldCase& c : cases) {
			TextureClass height(mapWidth, mapHeight, GL_R32F), colors(mapWidth, mapHeight, GL_RGBA32F), ids(mapWidth, mapHeight, GL_R32I);
			TextureClass splat(mapWidth, mapHeight, GL_RGBA8, GpuHeightGenerator::splatLayers);
			gpu.GenerateTerrain(c.terrain, c.area, height, &c.biomes);
			gpu.GenerateBiomes(c.biomes, c.terrain, c.area, colors, ids, &splat);
			const auto separate = std::array{ ReadBytes(height, GL_RED, GL_FLOAT, 4), ReadBytes(colors, GL_RGBA, GL_FLOAT, 16),
				ReadBytes(ids, GL_RED_INTEGER, GL_INT, 4), ReadBytes(splat, GL_RGBA, GL_UNSIGNED_BYTE, 4) };

			TextureClass height1(mapWidth, mapHeight, GL_R32F), colors1(mapWidth, mapHeight, GL_RGBA32F), ids1(mapWidth, mapHeight, GL_R32I);
			TextureClass splat1(mapWidth, mapHeight, GL_RGBA8, GpuHeightGenerator::splatLayers);
			gpu.GenerateWorld(c.terrain, c.biomes, c.area, height1, colors1, ids1, &splat1);
			const auto single = std::array{ ReadBytes(height1, GL_RED, GL_FLOAT, 4), ReadBytes(colors1, GL_RGBA, GL_FLOAT, 16),
				ReadBytes(ids1, GL_RED_INTEGER, GL_INT, 4), ReadBytes(splat1, GL_RGBA, GL_UNSIGNED_BYTE, 4) };

			const char* names[] = { "height", "colors", "ids", "splat" };
			const int bytes[] = { 4, 16, 4, 4 };
			std::string details;
			bool ok = true;
			for (int m = 0; m < 4; m++) {
				size_t different = 0;
				for (size_t i = 0; i < separate[m].size(); i += bytes[m]) {
					different += std::memcmp(&separate[m][i], &single[m][i], bytes[m]) != 0 ? 1 : 0;
				}
				ok = ok && different == 0;
				details += (m > 0 ? ", " : "") + std::string(names[m]) + " " + std::to_string(different);
			}
			allOk = allOk && ok;
			Report(c.name, details + " differ", ok);
		}
		return allOk;
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
		TerrainConfig nestedTerrain = NestedFromLayered(true);
		nestedTerrain.nested.root.points[2].sub.clear();
		nestedTerrain.nested.root.points[3].sub = { HandBuiltTree().points[2].sub[0] };
		check("chunk: nested terrain", GL_RED, GL_FLOAT, [&](const MapArea& area, TextureClass& t) { gpu.GenerateTerrain(nestedTerrain, area, t); });
		const BiomeConfig biomes = BiomeConfig::Default();
		check("chunk: biome modifiers", GL_RED, GL_FLOAT, [&](const MapArea& area, TextureClass& t) { gpu.GenerateTerrain(layered, area, t, &biomes); });
		check("chunk: biome ids", GL_RED_INTEGER, GL_INT, [&](const MapArea& area, TextureClass& ids) {
			TextureClass colors(ids.GetWidth(), ids.GetHeight(), GL_RGBA32F);
			gpu.GenerateBiomes(biomes, terrain, area, colors, ids);
		});
		check("chunk: blended biome colors", GL_RGBA, GL_FLOAT, [&](const MapArea& area, TextureClass& colors) {
			TextureClass ids(colors.GetWidth(), colors.GetHeight(), GL_R32I);
			gpu.GenerateBiomes(biomes, terrain, area, colors, ids);
		});

		//The splat map has several layers, so it is compared here rather than through check
		const auto whole = GenerateSplat(gpu, biomes, MapArea{}, mapWidth, mapHeight);
		for (int step : { 1, 2, 4 }) {
			const auto part = GenerateSplat(gpu, biomes, MapArea{ glm::vec2(corner), static_cast<float>(step), glm::ivec2(mapWidth, mapHeight) },
				partWidth, partHeight);
			int different = 0;
			for (int y = 0; y < partHeight; y++) {
				for (int x = 0; x < partWidth; x++) {
					different += whole[static_cast<size_t>(corner.y + y * step) * mapWidth + corner.x + x * step] != part[static_cast<size_t>(y) * partWidth + x] ? 1 : 0;
				}
			}
			const bool ok = different == 0;
			allOk = allOk && ok;
			Report("chunk: material splat, step " + std::to_string(step), std::to_string(different) + " of " + std::to_string(partWidth * partHeight) + " texels differ", ok);
		}

		//A partial map moved over the world (TerrainGenerationSys::MoveMap): the whole map size at a negative origin, in one pass as the
		//partial map is generated, equals the map of the world origin where they overlap
		{
			const glm::ivec2 moved(-517, 61);
			const MapArea area{ glm::vec2(moved), 1.0f, glm::ivec2(mapWidth, mapHeight) };
			TextureClass h0(mapWidth, mapHeight, GL_R32F), c0(mapWidth, mapHeight, GL_RGBA32F), i0(mapWidth, mapHeight, GL_R32I);
			TextureClass h1(mapWidth, mapHeight, GL_R32F), c1(mapWidth, mapHeight, GL_RGBA32F), i1(mapWidth, mapHeight, GL_R32I);
			gpu.GenerateWorld(layered, biomes, MapArea{}, h0, c0, i0);
			gpu.GenerateWorld(layered, biomes, area, h1, c1, i1);
			const size_t texels = static_cast<size_t>(mapWidth) * mapHeight;
			std::vector<float> height0(texels), height1(texels);
			std::vector<int> ids0(texels), ids1(texels);
			h0.Read(GL_RED, GL_FLOAT, static_cast<int>(texels * sizeof(float)), height0.data());
			h1.Read(GL_RED, GL_FLOAT, static_cast<int>(texels * sizeof(float)), height1.data());
			i0.Read(GL_RED_INTEGER, GL_INT, static_cast<int>(texels * sizeof(int)), ids0.data());
			i1.Read(GL_RED_INTEGER, GL_INT, static_cast<int>(texels * sizeof(int)), ids1.data());
			int different = 0, compared = 0;
			for (int y = 0; y < mapHeight; y++) {
				for (int x = 0; x < mapWidth; x++) {
					const glm::ivec2 world = moved + glm::ivec2(x, y);
					if (world.x < 0 || world.y < 0 || world.x >= mapWidth || world.y >= mapHeight) {
						continue;
					}
					const size_t a = static_cast<size_t>(world.y) * mapWidth + world.x, b = static_cast<size_t>(y) * mapWidth + x;
					compared++;
					different += std::memcmp(&height0[a], &height1[b], sizeof(float)) != 0 || ids0[a] != ids1[b] ? 1 : 0;
				}
			}
			const bool ok = different == 0 && compared > 0;
			allOk = allOk && ok;
			Report("chunk: moved map, height + ids",std::to_string(different) + " of " + std::to_string(compared) + " texels differ", ok);
		}
		return allOk;
	}

	//Map position (integer) of lowland near the middle of the default world: a little above the water, where both river methods carve
	glm::vec2 FindLowland(GpuHeightGenerator& gpu, const TerrainConfig& terrain)
	{
		constexpr int size = 256;
		constexpr float step = 64.0f;
		const HeightField s = gpu.SampleTerrain(terrain, glm::vec2(0.0f), size, step, glm::ivec2(mapWidth, mapHeight), nullptr);
		const float water = terrain.WaterHeight();
		glm::vec2 best(0.0f);
		float bestDistance = 1e30f;
		for (int y = 0; y < size; y++) {
			for (int x = 0; x < size; x++) {
				const float h = s.At(x, y);
				const glm::vec2 offset = glm::vec2(x, y) - glm::vec2(size / 2);
				//Sea 256 texels to the east, inside the checked map: the rivers have to stop at the coast
				const bool coast = x + 4 < size && s.At(x + 4, y) < water - 0.02f;
				if (h > water + 0.01f && h < water + 0.03f && coast && glm::dot(offset, offset) < bestDistance) {
					bestDistance = glm::dot(offset, offset);
					best = offset * step;
				}
			}
		}
		return best;
	}

	//Rivers carved into the terrain: values in [0, 1], never above the terrain without them, something carved; one pass (GenerateWorld)
	//equal to GenerateTerrain, and chunks equal to the whole map bit for bit, for both methods
	bool CheckRivers(GpuHeightGenerator& gpu)
	{
		std::cout << "\nrivers: carved terrain of the default world (with biome modifiers), network of a region\n";
		const TerrainConfig terrain = TerrainConfig::Default();
		const BiomeConfig biomes = BiomeConfig::Default();
		const glm::ivec2 island(mapWidth, mapHeight);
		const glm::vec2 center = FindLowland(gpu, terrain);
		const MapArea area{ center - glm::vec2(mapWidth / 2, mapHeight / 2), 1.0f, island };

		rivers::RiverConfig region;
		region.method = rivers::Method::REGION;
		constexpr int cells = 512;
		const float cellSize = static_cast<float>(region.cellSize);
		const auto start = std::chrono::steady_clock::now();
		const HeightField sample = gpu.SampleTerrain(terrain, center, cells, cellSize, island, &biomes);
		const hydrology::Network network = hydrology::Compute(sample, center - glm::vec2(cells * cellSize * 0.5f), cellSize, terrain.WaterHeight(), region);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		const std::vector<hydrology::WaterVertex> water = hydrology::BuildWaterMesh(network, terrain.WaterHeight(), 256.0f);
		RiverMap map;
		map.Upload(network, water);
		std::ostringstream networkDetails;
		networkDetails << network.riverCells << " river cells, " << network.lakes << " lakes, " << std::fixed << std::setprecision(0) << ms << " ms";
		bool allOk = network.riverCells > 0;
		Report("rivers: network of " + std::to_string(cells) + "^2 cells", networkDetails.str(), allOk);
		const bool meshOk = !water.empty();
		Report("rivers: water surface", std::to_string(water.size() / 6) + " quads", meshOk);
		allOk = allOk && meshOk;

		//The water shaders are drawn only in the application; this at least builds them (the river mesh with the shared fragment shader)
		const Shader seaShader("res/shaders/WaterShaders/water_vertex.shader", "res/shaders/WaterShaders/water_fragment.shader");
		const Shader meshShader("res/shaders/WaterShaders/water_mesh_vertex.shader", "res/shaders/WaterShaders/water_fragment.shader");
		const bool built = seaShader.IsValid() && meshShader.IsValid();
		Report("rivers: water shaders build", built ? "sea and rivers" : "see the errors above", built);
		allOk = allOk && built;

		rivers::RiverConfig noise;
		noise.method = rivers::Method::NOISE;
		const std::pair<std::string, const rivers::RiverConfig*> cases[] = { { "region", &region }, { "noise", &noise } };
		for (const auto& [name, config] : cases) {
			const RiverInput input{ *config, &map };
			//Without their humidity: it changes the biomes and so the biome modifiers, which may raise the terrain
			rivers::RiverConfig dryConfig = *config;
			dryConfig.humidity = 0.0f;
			const RiverInput dry{ dryConfig, &map };
			TextureClass plain(mapWidth, mapHeight, GL_R32F), carved(mapWidth, mapHeight, GL_R32F), wet(mapWidth, mapHeight, GL_R32F);
			gpu.GenerateTerrain(terrain, area, plain, &biomes);
			gpu.GenerateTerrain(terrain, area, carved, &biomes, &dry);
			gpu.GenerateTerrain(terrain, area, wet, &biomes, &input);
			const HeightField before = ReadHeight(plain), dryAfter = ReadHeight(carved), after = ReadHeight(wet);
			int lowered = 0, wrong = 0, sea = 0;
			float deepest = 0.0f;
			//Noise rivers end at the coast: the sea floor deeper than a channel stays as it was
			const bool noiseRivers = config->method == rivers::Method::NOISE;
			const float seaFloor = terrain.WaterHeight() - config->depthPerWidth * config->noiseHalfWidth;
			for (size_t i = 0; i < after.Size(); i++) {
				if (!std::isfinite(after[i]) || after[i] < 0.0f || after[i] > 1.0f || dryAfter[i] > before[i]) {
					wrong++;
				}
				if (noiseRivers && before[i] < seaFloor) {
					sea++;
					wrong += dryAfter[i] != before[i] ? 1 : 0;
				}
				lowered += dryAfter[i] < before[i] ? 1 : 0;
				deepest = std::max(deepest, before[i] - dryAfter[i]);
			}
			std::ostringstream details;
			details << lowered << " lowered, deepest " << std::fixed << std::setprecision(4) << deepest << ", " << wrong << " wrong";
			bool ok = wrong == 0 && lowered > 0 && (!noiseRivers || sea > 0);
			Report("rivers: " + name + " carves" + (noiseRivers ? ", sea " + std::to_string(sea) + " kept" : ""), details.str(), ok);
			allOk = allOk && ok;

			//One pass equals the terrain and the biomes generated apart (the humidity of rivers changes the biomes and their modifiers,
			//the bed material the splat map), and the bed shows in the splat map
			TextureClass height(mapWidth, mapHeight, GL_R32F), colors(mapWidth, mapHeight, GL_RGBA32F), ids(mapWidth, mapHeight, GL_R32I);
			TextureClass splat(mapWidth, mapHeight, GL_RGBA8, GpuHeightGenerator::splatLayers);
			gpu.GenerateWorld(terrain, biomes, area, height, colors, ids, &splat, &input);
			TextureClass colors2(mapWidth, mapHeight, GL_RGBA32F), ids2(mapWidth, mapHeight, GL_R32I);
			TextureClass splat2(mapWidth, mapHeight, GL_RGBA8, GpuHeightGenerator::splatLayers);
			gpu.GenerateBiomes(biomes, terrain, area, colors2, ids2, &splat2, &input);
			const HeightField single = ReadHeight(height);
			int different = 0;
			for (size_t i = 0; i < single.Size(); i++) {
				different += std::memcmp(&single[i], &after[i], sizeof(float)) != 0 ? 1 : 0;
			}
			const bool biomesSame = ReadBytes(colors, GL_RGBA, GL_FLOAT, 16) == ReadBytes(colors2, GL_RGBA, GL_FLOAT, 16)
				&& ReadBytes(ids, GL_RED_INTEGER, GL_INT, 4) == ReadBytes(ids2, GL_RED_INTEGER, GL_INT, 4);
			const std::vector<unsigned char> splatBytes = ReadBytes(splat, GL_RGBA, GL_UNSIGNED_BYTE, 4);
			const bool splatSame = splatBytes == ReadBytes(splat2, GL_RGBA, GL_UNSIGNED_BYTE, 4);
			//Texels where the bed material took over only because of the river (some biomes use it too).
			//Material m is byte (m / 4) layer, m % 4 channel; layers follow each other
			TextureClass colors3(mapWidth, mapHeight, GL_RGBA32F), ids3(mapWidth, mapHeight, GL_R32I);
			TextureClass splat3(mapWidth, mapHeight, GL_RGBA8, GpuHeightGenerator::splatLayers);
			gpu.GenerateBiomes(biomes, terrain, area, colors3, ids3, &splat3);
			const std::vector<unsigned char> noRiverBytes = ReadBytes(splat3, GL_RGBA, GL_UNSIGNED_BYTE, 4);
			const size_t layerBytes = static_cast<size_t>(mapWidth) * mapHeight * 4;
			int bedTexels = 0;
			for (size_t t = 0; t < static_cast<size_t>(mapWidth) * mapHeight; t++) {
				const size_t b = (config->bedMaterial / 4) * layerBytes + t * 4 + config->bedMaterial % 4;
				bedTexels += splatBytes[b] > 200 && noRiverBytes[b] < 100 ? 1 : 0;
			}
			ok = different == 0 && biomesSame && splatSame && bedTexels > 0;
			Report("rivers: " + name + ", one pass", std::to_string(different) + " heights differ, biomes " + (biomesSame && splatSame ? "same" : "DIFFER")
				+ ", bed " + std::to_string(bedTexels), ok);
			allOk = allOk && ok;

			constexpr int partWidth = 131, partHeight = 50;
			const glm::ivec2 corner(300, 30);
			for (int step : { 1, 2, 4 }) {
				TextureClass part(partWidth, partHeight, GL_R32F);
				gpu.GenerateTerrain(terrain, MapArea{ area.origin + glm::vec2(corner), static_cast<float>(step), island }, part, &biomes, &input);
				const HeightField p = ReadHeight(part);
				different = 0;
				for (int y = 0; y < partHeight; y++) {
					for (int x = 0; x < partWidth; x++) {
						different += std::memcmp(&p.At(x, y), &after.At(corner.x + x * step, corner.y + y * step), sizeof(float)) != 0 ? 1 : 0;
					}
				}
				ok = different == 0;
				Report("rivers: " + name + ", chunk step " + std::to_string(step), std::to_string(different) + " of " + std::to_string(partWidth * partHeight)
					+ " texels differ", ok);
				allOk = allOk && ok;
			}
		}
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

	//The river sample read without stalling: the same values as the synchronous read, available once the GPU is done
	bool CheckAsync(GpuHeightGenerator& gpu)
	{
		std::cout << "\nasync: reads of the GPU through a pixel buffer\n";
		const TerrainConfig config = TerrainConfig::Default();
		const glm::vec2 center(1500.0f, -700.0f);
		constexpr int size = 96;
		constexpr float step = 4.0f;
		const HeightField sync = gpu.SampleTerrain(config, center, size, step, glm::ivec2(1024, 768), nullptr);
		AsyncRead read;
		gpu.StartSampleTerrain(config, center, size, step, glm::ivec2(1024, 768), nullptr, read);
		const bool pending = read.Pending();
		glFinish();
		const bool ready = read.Ready();
		HeightField async(size, size);
		const bool taken = read.Take(async.Data(), async.Size() * sizeof(float));
		const bool same = taken && std::memcmp(sync.Data(), async.Data(), sync.Size() * sizeof(float)) == 0;
		Report("async: sample equals the synchronous read", pending && ready && taken ? "" : "not pending / ready / taken", same && !read.Pending());
		//What the frame pays for the river sample of the largest region (2048 cells): the synchronous read waits for the GPU, the async one only sends
		glFinish();
		auto ms = [](auto from) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count(); };
		auto start = std::chrono::steady_clock::now();
		gpu.SampleTerrain(config, center, 2048, step, glm::ivec2(1024, 768), nullptr);
		const double syncMs = ms(start);
		glFinish();
		start = std::chrono::steady_clock::now();
		gpu.StartSampleTerrain(config, center, 2048, step, glm::ivec2(1024, 768), nullptr, read);
		const double asyncMs = ms(start);
		glFinish();
		read.Cancel();
		std::ostringstream timing;
		timing << std::fixed << std::setprecision(1) << "sync " << syncMs << " ms, async start " << asyncMs << " ms";
		Report("async: 2048² sample, time in the frame", timing.str(), true);
		return same && pending && ready && !read.Pending();
	}

	//A sculpt dab changes a rectangle of heights; the surface updated only around it must equal a pass over the whole map, also where the
	//rectangle touches the map edge (there the Laplacian reaches 2 texels in)
	bool CheckSurfaceRegion(GpuSurfacePass& pass)
	{
		constexpr int width = 40, height = 30;
		auto fill = [](HeightField& map, float bump, glm::ivec4 rect) {
			for (int y = 0; y < map.GetHeight(); y++) {
				for (int x = 0; x < map.GetWidth(); x++) {
					const bool inside = x >= rect.x && y >= rect.y && x < rect.x + rect.z && y < rect.y + rect.w;
					map.At(x, y) = std::sin(0.37f * x) * std::cos(0.23f * y) + 0.01f * x * y + (inside ? bump * (1.0f + 0.1f * x - 0.07f * y) : 0.0f);
				}
			}
		};
		bool ok = true;
		for (const auto& [name, rect] : { std::pair{ "surface: rectangle inside", glm::ivec4(12, 9, 7, 5) },
				{ "surface: rectangle at the left edge", glm::ivec4(0, 0, 3, height) }, { "surface: rectangle in a corner", glm::ivec4(37, 27, 3, 3) },
				{ "surface: rectangle near the edge", glm::ivec4(2, 6, 4, 5) } }) {
			HeightField before(width, height), after(width, height);
			fill(before, 0.0f, rect);
			fill(after, 0.75f, rect);
			TextureClass input(width, height, GL_R32F), updated(width, height, GL_RGBA32F), whole(width, height, GL_RGBA32F);
			input.Upload(before.Data(), GL_RED, GL_FLOAT);
			pass.Run(input, surfaceTexelSize, updated);
			input.Upload(after.Data(), GL_RED, GL_FLOAT);
			pass.RunRegion(input, surfaceTexelSize, updated, rect);
			pass.Run(input, surfaceTexelSize, whole);
			std::vector<Derivatives> a(static_cast<size_t>(width) * height), b(a.size());
			updated.Read(GL_RGBA, GL_FLOAT, static_cast<int>(a.size() * sizeof(Derivatives)), a.data());
			whole.Read(GL_RGBA, GL_FLOAT, static_cast<int>(b.size() * sizeof(Derivatives)), b.data());
			const bool same = std::memcmp(a.data(), b.data(), a.size() * sizeof(Derivatives)) == 0;
			Report(name, "equals the whole pass", same);
			ok = ok && same;
		}
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

	//Map for erosion: h(x, y) given in texels
	HeightField ErosionInput(int width, int height, const std::function<float(int, int)>& h)
	{
		HeightField map(width, height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				map.At(x, y) = h(x, y);
			}
		}
		return map;
	}

	//Erodes input with config and returns the result
	HeightField Erode(GpuErosion& erosion, const erosion::ErosionConfig& config, const HeightField& input, uint32_t seed)
	{
		TextureClass texture(input.GetWidth(), input.GetHeight(), GL_R32F);
		texture.Upload(input.Data(), GL_RED, GL_FLOAT);
		erosion.Erode(config, texture, texture, seed);
		return ReadHeight(texture);
	}

	struct ErosionStats {
		size_t nonFinite = 0, changed = 0;
		float largestChange = 0.0f, low = INFINITY, high = -INFINITY;
	};

	ErosionStats CompareMaps(const HeightField& before, const HeightField& after)
	{
		ErosionStats s;
		for (size_t i = 0; i < before.Size(); i++) {
			const float a = after.Data()[i];
			if (!std::isfinite(a)) {
				s.nonFinite++;
				continue;
			}
			const float change = std::abs(a - before.Data()[i]);
			s.changed += change > 0.0f ? 1 : 0;
			s.largestChange = std::max(s.largestChange, change);
			s.low = std::min(s.low, a);
			s.high = std::max(s.high, a);
		}
		return s;
	}

	bool CheckErosion(GpuHeightGenerator& gpu, GpuErosion& erosion)
	{
		std::cout << "\nerosion: droplets\n";
		//Not square and smaller than the generation maps: the steep slope below has to stay within the fixed point range of the shader
		constexpr int width = 300, height = 170;
		bool ok = true;

		//Without droplets only the conversion to fixed point (steps of 2^-24) and back touches the map: at most half a step
		{
			const HeightField input = ErosionInput(width, height, [](int x, int y) { return std::sin(0.05f * x) * std::cos(0.07f * y); });
			erosion::ErosionConfig config;
			config.dropletCount = 0;
			const ErosionStats s = CompareMaps(input, Erode(erosion, config, input, 1));
			const bool caseOk = s.nonFinite == 0 && s.largestChange <= 0.5f / 16777216.0f * 1.01f;
			std::ostringstream details;
			details << "largest change " << std::scientific << std::setprecision(1) << s.largestChange;
			Report("erosion: no droplets keep the map", details.str(), caseOk);
			ok = ok && caseOk;
		}

		//A steep slope high above 0 with slow droplets: a hole would come from a broken velocity or capacity (NaN), not from erosion
		{
			const HeightField input = ErosionInput(width, height, [](int x, int y) { return 5.0f + 0.1f * x + 0.05f * y; });
			erosion::ErosionConfig config;
			config.dropletCount = 5000;
			config.dropletLifetime = 32;
			config.initialVelocity = 0.1f;
			const HeightField output = Erode(erosion, config, input, 2);
			const ErosionStats s = CompareMaps(input, output);
			const bool caseOk = s.nonFinite == 0 && s.low > 1.0f && s.changed > 0;
			std::ostringstream details;
			details << "lowest " << std::fixed << std::setprecision(3) << s.low << ", changed " << s.changed;
			Report("erosion: steep slope without holes", details.str(), caseOk);
			ok = ok && caseOk;
		}

		//Default parameters on a noise map: finite, changed, no large spikes; a second run on the result changes it again
		{
			TextureClass noiseMap(width, height, GL_R32F);
			gpu.GenerateNoise(noise::NoiseConfigParameters{}, MapArea{}, noiseMap);
			const HeightField input = ReadHeight(noiseMap);
			const erosion::ErosionConfig config;
			const HeightField once = Erode(erosion, config, input, 3);
			const HeightField twice = Erode(erosion, config, once, 4);
			const ErosionStats s = CompareMaps(input, once);
			const ErosionStats again = CompareMaps(once, twice);
			const bool caseOk = s.nonFinite == 0 && again.nonFinite == 0 && s.changed > input.Size() / 20 && again.changed > input.Size() / 20
				&& s.low >= -1.5f && s.high <= 1.5f && s.largestChange < 0.5f;
			std::ostringstream details;
			details << "changed " << s.changed << " then " << again.changed << ", range " << std::fixed << std::setprecision(2) << s.low << ".." << s.high;
			Report("erosion: noise map, twice", details.str(), caseOk);
			ok = ok && caseOk;
		}

		std::cout << "\nerosion: water flow (pipes)\n";
		erosion::ErosionConfig pipes;
		pipes.method = erosion::Method::PIPES;
		TextureClass noiseMap(width, height, GL_R32F);
		gpu.GenerateNoise(noise::NoiseConfigParameters{}, MapArea{}, noiseMap);
		const HeightField noiseInput = ReadHeight(noiseMap);

		//No iterations: only scaling by heightScale (a power of two) and back
		{
			erosion::ErosionConfig config = pipes;
			config.pipes.iterations = 0;
			const ErosionStats s = CompareMaps(noiseInput, Erode(erosion, config, noiseInput, 0));
			const bool caseOk = s.nonFinite == 0 && s.changed == 0;
			Report("pipes: no iterations keep the map", "changed " + std::to_string(s.changed), caseOk);
			ok = ok && caseOk;
		}

		//Every texel writes only its own texels, so two runs are equal bit for bit
		{
			const HeightField first = Erode(erosion, pipes, noiseInput, 0);
			const HeightField second = Erode(erosion, pipes, noiseInput, 0);
			const size_t different = CompareMaps(first, second).changed;
			const bool caseOk = different == 0;
			Report("pipes: deterministic", "different " + std::to_string(different), caseOk);
			ok = ok && caseOk;
		}

		//Thermal erosion alone (no rain) on a step much steeper than the talus angle: moves material without losing it
		//and leaves no slope much steeper than the talus angle
		{
			erosion::ErosionConfig config = pipes;
			config.pipes.rainRate = 0.0f;
			config.pipes.thermalRate = 5.0f;
			config.pipes.timeStep = 0.2f;
			config.pipes.iterations = 2000;
			const HeightField input = ErosionInput(width, height, [](int x, int y) { return (x - 150) * (x - 150) + (y - 85) * (y - 85) < 900 ? 1.0f : 0.0f; });
			const HeightField output = Erode(erosion, config, input, 0);
			double before = 0.0, after = 0.0;
			float steepest = 0.0f;
			for (int y = 0; y < height; y++) {
				for (int x = 0; x < width; x++) {
					before += input.At(x, y);
					after += output.At(x, y);
					if (x + 1 < width) {
						steepest = std::max(steepest, std::abs(output.At(x + 1, y) - output.At(x, y)));
					}
					if (y + 1 < height) {
						steepest = std::max(steepest, std::abs(output.At(x, y + 1) - output.At(x, y)));
					}
				}
			}
			//In texels: the slope at the talus angle is tan(35 deg) = 0.70
			const float steepestSlope = steepest * config.pipes.heightScale;
			const double lost = std::abs(after - before) / before;
			const bool caseOk = lost < 1e-4 && steepestSlope < 0.8f;
			std::ostringstream details;
			details << "steepest " << std::fixed << std::setprecision(2) << steepestSlope << " (talus 0.70), lost " << std::scientific << std::setprecision(1) << lost;
			Report("pipes: thermal keeps material, talus", details.str(), caseOk);
			ok = ok && caseOk;
		}

		//Default parameters on a noise map: finite, changed, no spikes, little material lost (some leaves with the water over the edges)
		{
			const HeightField output = Erode(erosion, pipes, noiseInput, 0);
			const ErosionStats s = CompareMaps(noiseInput, output);
			double before = 0.0, after = 0.0;
			for (size_t i = 0; i < output.Size(); i++) {
				before += noiseInput.Data()[i];
				after += output.Data()[i];
			}
			const double lost = (before - after) / before;
			const bool caseOk = s.nonFinite == 0 && s.changed > noiseInput.Size() / 2 && s.low >= -1.5f && s.high <= 1.5f && s.largestChange < 0.5f
				&& std::abs(lost) < 0.05;
			std::ostringstream details;
			details << "changed " << s.changed << ", largest " << std::fixed << std::setprecision(3) << s.largestChange << ", lost " << std::setprecision(4) << lost;
			Report("pipes: noise map", details.str(), caseOk);
			ok = ok && caseOk;

			//Spikes: texels above all four neighbours, in texels. Sediment dropped at once in thin streams made lines of them, up to 10 texels high;
			//settling is limited to the highest neighbour + the talus height (0.70 at 35 degrees)
			float highest = 0.0f;
			for (int y = 1; y < height - 1; y++) {
				for (int x = 1; x < width - 1; x++) {
					const float neighbours = std::max({ output.At(x - 1, y), output.At(x + 1, y), output.At(x, y - 1), output.At(x, y + 1) });
					highest = std::max(highest, (output.At(x, y) - neighbours) * pipes.pipes.heightScale);
				}
			}
			const bool spikesOk = highest <= 0.8f;
			std::ostringstream spikeDetails;
			spikeDetails << "highest above neighbours " << std::fixed << std::setprecision(2) << highest << " texels";
			Report("pipes: no spikes", spikeDetails.str(), spikesOk);
			ok = ok && spikesOk;
		}
		return ok;
	}

	//Every brush on the GPU against sculpt::Apply on the CPU, over the whole map (texels outside the brush must not change)
	bool CheckSculpt(GpuSculpt& sculptor)
	{
		std::cout << "\nsculpt\n";
		constexpr int width = 200, height = 120;
		const HeightField input = ErosionInput(width, height, [](int x, int y) { return 0.9f * std::sin(0.11f * x) * std::cos(0.07f * y); });

		struct SculptCase {
			const char* name;
			sculpt::Brush brush;
			float radius, hardness, amount;
			glm::vec2 center;
		};
		//Radii from small to larger than the map, so the copy of the heights from before the dab has to grow
		const SculptCase cases[] = {
			{ "sculpt: raise, hard, small", sculpt::Brush::RAISE, 3.5f, 1.0f, 0.05f, { 100.0f, 60.0f } },
			{ "sculpt: raise at the edge", sculpt::Brush::RAISE, 24.0f, 0.5f, 0.05f, { -3.0f, 50.0f } },
			{ "sculpt: raise to the limit", sculpt::Brush::RAISE, 40.0f, 0.3f, 0.9f, { 57.0f, 0.0f } },
			{ "sculpt: lower", sculpt::Brush::LOWER, 24.0f, 0.5f, 0.05f, { 100.3f, 61.7f } },
			{ "sculpt: lower at the corner", sculpt::Brush::LOWER, 30.0f, 0.0f, 0.4f, { 198.0f, 119.5f } },
			{ "sculpt: smooth", sculpt::Brush::SMOOTH, 24.0f, 0.5f, 0.8f, { 100.3f, 61.7f } },
			{ "sculpt: smooth at the edge", sculpt::Brush::SMOOTH, 50.0f, 0.5f, 1.0f, { 190.0f, 3.0f } },
			{ "sculpt: flatten", sculpt::Brush::FLATTEN, 24.0f, 0.5f, 0.6f, { 100.3f, 61.7f } },
			{ "sculpt: noise", sculpt::Brush::NOISE, 24.0f, 0.5f, 0.3f, { 100.3f, 61.7f } },
			{ "sculpt: noise larger than the map", sculpt::Brush::NOISE, 300.0f, 0.5f, 0.3f, { 80.0f, 70.0f } },
			{ "sculpt: smooth larger than the map", sculpt::Brush::SMOOTH, 300.0f, 0.5f, 1.0f, { 80.0f, 70.0f } },
		};
		bool ok = true;
		for (const SculptCase& c : cases) {
			sculpt::BrushConfig config;
			config.brush = c.brush;
			config.radius = c.radius;
			config.hardness = c.hardness;
			config.noiseScale = 9.0f;
			const sculpt::Dab dab{ c.center, c.amount, 0.1f, 12345u, 1.5f };

			HeightField expected = input;
			sculpt::Apply(expected, config, dab);
			TextureClass texture(width, height, GL_R32F);
			texture.Upload(input.Data(), GL_RED, GL_FLOAT);
			sculptor.Apply(config, dab, texture);
			const HeightField result = ReadHeight(texture);

			float largest = 0.0f;
			int changed = 0;
			bool finite = true;
			for (size_t i = 0; i < result.Size(); i++) {
				finite = finite && std::isfinite(result[i]);
				largest = std::max(largest, std::abs(result[i] - expected[i]));
				changed += expected[i] != input[i] ? 1 : 0;
			}
			const bool caseOk = finite && largest <= 1e-5f && changed > 0;
			std::ostringstream details;
			details << "largest difference " << std::scientific << std::setprecision(1) << largest << ", changed " << changed;
			Report(c.name, details.str(), caseOk);
			ok = ok && caseOk;
		}
		return ok;
	}

	//Biomes of a map that was not generated (GenerateMapBiomes) against the CPU rules: mapbiomes::ClimateHeight, biome::ApplyClimate,
	//Nearest, BlendWeight and MixMaterials
	struct MapBiomesRun {
		std::vector<int> ids;
		std::vector<float> colors;		//RGBA
		std::vector<unsigned char> splat;	//layer after layer, RGBA
	};

	bool CheckMapBiomes(GpuHeightGenerator& gpu)
	{
		std::cout << "\nmapbiomes: biomes of a map from its climate\n";
		const int width = mapWidth, height = mapHeight;
		const size_t texels = static_cast<size_t>(width) * height;
		const float sea = 0.0f, metresPerUnit = 300.0f;
		const glm::dvec2 pixel(2.0, 3.0);
		HeightField metres(width, height), units(width, height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				metres.At(x, y) = -50.0f + 350.0f * x / (width - 1) + 20.0f * std::sin(0.5f * y);
				units.At(x, y) = metres.At(x, y) / metresPerUnit;
			}
		}
		mapbiomes::Settings settings = mapbiomes::Default(sea);
		settings.coastMetres = 400.0f;
		settings.reliefRadiusMetres = 6.0f;
		settings.reliefMetres = 50.0f;
		//Temperature and humidity cover the whole range in every biome, so the result does not depend on their noises
		for (biome::Biome& b : settings.config.table) {
			b.ranges[biome::TEMPERATURE] = biome::Range{};
			b.ranges[biome::HUMIDITY] = biome::Range{};
		}
		settings.config.materialPatchiness = 0.0f;
		const mapbiomes::ClimateMaps climate = mapbiomes::ComputeClimate(metres, pixel, sea, settings);

		TextureClass heightTexture(width, height, GL_R32F), climateTexture(width, height, GL_RG16F);
		heightTexture.Upload(units.Data(), GL_RED, GL_FLOAT);
		climateTexture.Upload(climate.packed.data(), GL_RG, GL_HALF_FLOAT);
		struct Targets {
			TextureClass colors{ mapWidth, mapHeight, GL_RGBA32F };
			TextureClass ids{ mapWidth, mapHeight, GL_R32I };
			TextureClass splat{ mapWidth, mapHeight, GL_RGBA8, GpuHeightGenerator::splatLayers };
		};
		auto runWith = [&](const mapbiomes::Settings& s, const MapBiomeInput& in, Targets& t) {
			gpu.GenerateMapBiomes(s, sea, in, t.colors, t.ids, &t.splat);
			MapBiomesRun result;
			result.ids.resize(texels);
			result.colors.resize(texels * 4);
			result.splat.resize(texels * 4 * GpuHeightGenerator::splatLayers);
			t.ids.Read(GL_RED_INTEGER, GL_INT, static_cast<int>(result.ids.size() * sizeof(int)), result.ids.data());
			t.colors.Read(GL_RGBA, GL_FLOAT, static_cast<int>(result.colors.size() * sizeof(float)), result.colors.data());
			t.splat.Read(GL_RGBA, GL_UNSIGNED_BYTE, static_cast<int>(result.splat.size()), result.splat.data());
			return result;
		};
		auto input = [&](glm::vec2 texelMetres) { return MapBiomeInput{ heightTexture, climateTexture, metresPerUnit, texelMetres }; };
		auto run = [&](const mapbiomes::Settings& s, glm::vec2 texelMetres) {
			Targets t;
			return runWith(s, input(texelMetres), t);
		};

		//The CPU rules for every texel against a GPU result; paint = nullptr: nothing painted
		struct Comparison {
			size_t idMismatch = 0, ambiguous = 0, kinds = 0;
			float colorError = 0.0f, splatError = 0.0f;
		};
		//classes, masks: the land cover of every texel and the allowed biomes of every class (nullptr = none)
		auto compare = [&](const MapBiomesRun& gpuRun, const mapbiomes::Settings& s, const biomepaint::Layer* paint,
			const std::vector<uint8_t>* classes = nullptr, const std::array<uint64_t, 256>* masks = nullptr) {
			Comparison result;
			const BiomeConfig& config = s.config;
			const float water = mapbiomes::WaterHeight(s, sea);
			std::array<int, 256> slots{};
			slots.fill(-1);
			if (paint) {
				slots = biomepaint::SlotBiomes(*paint, config.table);
			}
			std::array<size_t, biome::maxBiomes> seen{};
			const biome::MaterialWeights noPatch{};
			for (int y = 0; y < height; y++) {
				for (int x = 0; x < width; x++) {
					const size_t i = static_cast<size_t>(y) * width + x;
					const glm::vec2 cm = glm::unpackHalf2x16(climate.packed[i]);
					const float h = mapbiomes::ClimateHeight(metres[i], cm.x, sea, s);
					const biome::ParameterValues values = biome::ApplyClimate({ cm.x, cm.y, 0.0f, 0.0f, 0.0f, 0.0f }, h, water, config.climate);
					const uint64_t mask = classes && (*masks)[(*classes)[i]] != 0 ? (*masks)[(*classes)[i]] : biome::allBiomes;
					int id = biome::Nearest(config.table, values, mask);
					const float nearest = config.table[id].Distance(values);
					//A texel where two allowed biomes are almost equally near may go either way on the GPU
					float second = 1e30f;
					for (size_t b = 0; b < config.table.size(); b++) {
						if (static_cast<int>(b) != id && ((mask >> b) & 1) != 0) {
							second = std::min(second, config.table[b].Distance(values));
						}
					}
					const bool tie = second - nearest < 1e-4f;
					glm::vec3 color = biome::MixColor(config.table, values, config.blendWidth, mask);
					biome::MaterialWeights mix = biome::MixMaterials(config.table, values, config.materialBlendWidth, 0.0f, config.materialPatchContrast, noPatch,
						mask);
					bool paintedId = false;
					if (paint) {
						const int slot = paint->texels[i] & 0xFF, strength = paint->texels[i] >> 8;
						const int b = slots[slot];
						if (b >= 0 && strength > 0) {
							const float share = strength / 255.0f;
							color = color + (config.table[b].color - color) * share;
							mix = biomepaint::PaintedMaterials(mix, config.table[b], share);
							if (strength >= 128) {
								id = b;
								paintedId = true;
							}
						}
					}
					if (gpuRun.ids[i] != id) {
						(tie && !paintedId ? result.ambiguous : result.idMismatch)++;
						continue;
					}
					seen[id]++;
					for (int c = 0; c < 3; c++) {
						result.colorError = std::max(result.colorError, std::abs(gpuRun.colors[i * 4 + c] - color[c]));
					}
					for (int m = 0; m < biome::maxMaterials; m++) {
						const float share = gpuRun.splat[((m / 4) * texels + i) * 4 + m % 4] / 255.0f;
						result.splatError = std::max(result.splatError, std::abs(share - mix[m]));
					}
				}
			}
			result.kinds = static_cast<size_t>(std::count_if(seen.begin(), seen.end(), [](size_t c) { return c > 0; }));
			return result;
		};
		auto matches = [&](const Comparison& c) {
			return c.idMismatch == 0 && c.ambiguous < texels / 1000 && c.colorError <= 1e-5f && c.splatError <= 1.0f / 255.0f + 1e-4f;
		};
		auto details = [](const Comparison& c) {
			std::ostringstream text;
			text << c.idMismatch << " wrong ids, " << c.ambiguous << " ties, largest difference " << std::scientific << std::setprecision(1)
				<< std::max(c.colorError, c.splatError);
			return text.str();
		};

		const MapBiomesRun gpuRun = run(settings, glm::vec2(pixel));
		const Comparison plain = compare(gpuRun, settings, nullptr);
		const bool idsOk = plain.idMismatch == 0 && plain.ambiguous < texels / 1000 && plain.kinds > 2;
		Report("mapbiomes: ids match the CPU", std::to_string(plain.kinds) + " biomes, " + std::to_string(plain.idMismatch) + " wrong, "
			+ std::to_string(plain.ambiguous) + " ties", idsOk);
		std::ostringstream colorDetails, splatDetails;
		colorDetails << "largest difference " << std::scientific << std::setprecision(1) << plain.colorError;
		splatDetails << "largest difference " << std::scientific << std::setprecision(1) << plain.splatError;
		const bool colorsOk = plain.colorError <= 1e-5f;
		const bool splatOk = plain.splatError <= 1.0f / 255.0f + 1e-4f;
		Report("mapbiomes: colors match the CPU", colorDetails.str(), colorsOk);
		Report("mapbiomes: splat matches the CPU", splatDetails.str(), splatOk);

		//Painted biomes: a strong patch with a soft edge and a weak one; the CPU mix (biomepaint) against the shader
		const std::vector<biome::Biome>& table = settings.config.table;
		TextureClass paintTexture(width, height, GL_RG8UI);
		auto paintDab = [](glm::vec2 center, float radius, float hardness, float amount, uint8_t slot) {
			biomepaint::Dab dab;
			dab.center = center;
			dab.radius = radius;
			dab.hardness = hardness;
			dab.amount = amount;
			dab.slot = slot;
			return dab;
		};
		auto paintedInput = [&](const biomepaint::Layer& layer, const std::vector<biome::Biome>& biomes) {
			paintTexture.UploadRegion(0, 0, width, height, width, layer.texels.data(), GL_RG_INTEGER, GL_UNSIGNED_BYTE);
			MapBiomeInput in = input(glm::vec2(pixel));
			in.paint = &paintTexture;
			in.paintBiomes = biomepaint::SlotBiomes(layer, biomes);
			return in;
		};
		biomepaint::Layer paint = biomepaint::Make(width, height);
		const uint8_t strongSlot = biomepaint::SlotOf(paint, table[2].name), weakSlot = biomepaint::SlotOf(paint, table[4].name);
		biomepaint::Apply(paint, paintDab({ 300.0f, 100.0f }, 60.0f, 0.3f, 1.0f, strongSlot));
		biomepaint::Apply(paint, paintDab({ 700.0f, 150.0f }, 50.0f, 0.0f, 0.25f, weakSlot));
		Targets paintTargets;
		const MapBiomesRun paintedRun = runWith(settings, paintedInput(paint, table), paintTargets);
		const Comparison painted = compare(paintedRun, settings, &paint);
		size_t paintChanged = 0;
		for (size_t i = 0; i < texels; i++) {
			paintChanged += paintedRun.ids[i] != gpuRun.ids[i] ? 1 : 0;
		}
		const bool paintOk = matches(painted) && paintChanged > 0;
		Report("mapbiomes: paint matches the CPU", details(painted) + ", " + std::to_string(paintChanged) + " ids painted", paintOk);

		//Names: the table reversed and a name that is not in it (automatic biomes there)
		mapbiomes::Settings reversed = settings;
		std::reverse(reversed.config.table.begin(), reversed.config.table.end());
		biomepaint::Layer named = paint;
		biomepaint::Apply(named, paintDab({ 500.0f, 200.0f }, 40.0f, 1.0f, 1.0f, biomepaint::SlotOf(named, "No such biome")));
		Targets namedTargets;
		const Comparison byName = compare(runWith(reversed, paintedInput(named, reversed.config.table), namedTargets), reversed, &named);
		const bool namesOk = matches(byName);
		Report("mapbiomes: paint follows names", details(byName), namesOk);

		//Land cover (stage D3): bands of WorldCover classes across the map (and no data), the masks of mapbiomes::ClassMasks
		std::vector<uint8_t> classes(texels);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const uint8_t bands[4] = { 10, 30, 60, 0 };
				classes[static_cast<size_t>(y) * width + x] = bands[std::min(3, 4 * x / width)];
			}
		}
		TextureClass classTexture(width, height, GL_R8UI);
		classTexture.UploadRegion(0, 0, width, height, width, classes.data(), GL_RED_INTEGER, GL_UNSIGNED_BYTE);
		mapbiomes::Settings covered = settings;
		covered.source = mapbiomes::Source::LAND_COVER;
		const std::array<uint64_t, 256> classMasks = mapbiomes::ClassMasks(covered);
		auto coveredInput = [&](MapBiomeInput in, const std::array<uint64_t, 256>& masks) {
			in.classes = &classTexture;
			in.classMasks = masks;
			return in;
		};
		Targets coverTargets;
		const MapBiomesRun coverRun = runWith(settings, coveredInput(input(glm::vec2(pixel)), classMasks), coverTargets);
		const Comparison cover = compare(coverRun, settings, nullptr, &classes, &classMasks);
		size_t coverChanged = 0;
		for (size_t i = 0; i < texels; i++) {
			coverChanged += coverRun.ids[i] != gpuRun.ids[i] ? 1 : 0;
		}
		const bool coverOk = matches(cover) && coverChanged > 0;
		Report("mapbiomes: land cover limits the biomes", details(cover) + ", " + std::to_string(coverChanged) + " ids changed", coverOk);

		//Masks of 0 (classes without biomes): bit for bit the biomes without land cover
		Targets emptyTargets;
		const MapBiomesRun emptyRun = runWith(settings, coveredInput(input(glm::vec2(pixel)), std::array<uint64_t, 256>{}), emptyTargets);
		const bool emptySame = emptyRun.ids == gpuRun.ids && emptyRun.colors == gpuRun.colors && emptyRun.splat == gpuRun.splat;
		Report("mapbiomes: empty masks equal no land cover", emptySame ? "bit for bit" : "results differ", emptySame);

		//Painting goes over the land cover
		Targets paintCoverTargets;
		const MapBiomesRun paintCoverRun = runWith(settings, coveredInput(paintedInput(paint, table), classMasks), paintCoverTargets);
		const Comparison paintCover = compare(paintCoverRun, settings, &paint, &classes, &classMasks);
		const bool paintCoverOk = matches(paintCover);
		Report("mapbiomes: paint over land cover", details(paintCover), paintCoverOk);

		//The LAND COVER display mode: the terrain program links and keeps the class map and its colors (TerrainView::Draw sets them)
		const std::unique_ptr<Shader> terrainShader = TerrainView::CreateTerrainShader();
		const bool terrainLinked = terrainShader && terrainShader->IsValid();
		const bool landCoverUniforms = terrainLinked && glGetUniformLocation(terrainShader->GetRendererID(), "landCoverMap") >= 0
			&& glGetUniformLocation(terrainShader->GetRendererID(), "landCoverColors") >= 0;
		const bool displayOk = terrainLinked && landCoverUniforms;
		Report("mapbiomes: LAND COVER display mode", !terrainLinked ? "terrain shader does not link" : landCoverUniforms ? "uniforms present"
			: "class map uniforms missing", displayOk);

		//A pass over a rectangle after a pass over the whole map: inside it the result of the whole map with the new paint, outside the old one
		biomepaint::Layer more = paint;
		biomepaint::Apply(more, paintDab({ 790.0f, 110.0f }, 70.0f, 0.5f, 1.0f, strongSlot));
		const glm::ivec4 region(300, 40, 500, 150);
		MapBiomeInput regionInput = paintedInput(more, table);
		regionInput.region = region;
		const MapBiomesRun regionRun = runWith(settings, regionInput, paintTargets);
		Targets wholeTargets;
		const MapBiomesRun wholeRun = runWith(settings, paintedInput(more, table), wholeTargets);
		size_t regionWrong = 0, outsideChanged = 0;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const size_t i = static_cast<size_t>(y) * width + x;
				const bool inside = x >= region.x && y >= region.y && x < region.x + region.z && y < region.y + region.w;
				const MapBiomesRun& expected = inside ? wholeRun : paintedRun;
				bool same = regionRun.ids[i] == expected.ids[i];
				for (int c = 0; c < 4; c++) {
					same = same && regionRun.colors[i * 4 + c] == expected.colors[i * 4 + c];
				}
				for (int l = 0; l < GpuHeightGenerator::splatLayers; l++) {
					for (int c = 0; c < 4; c++) {
						const size_t at = (static_cast<size_t>(l) * texels + i) * 4 + c;
						same = same && regionRun.splat[at] == expected.splat[at];
					}
				}
				regionWrong += same ? 0 : 1;
				outsideChanged += !inside && wholeRun.ids[i] != paintedRun.ids[i] ? 1 : 0;
			}
		}
		//The new dab reaches past the rectangle, so a pass over the whole map differs there: the region pass must have kept the old result
		const bool regionOk = regionWrong == 0 && outsideChanged > 0;
		Report("mapbiomes: region equals the whole map", std::to_string(regionWrong) + " texels differ, " + std::to_string(outsideChanged)
			+ " kept outside", regionOk);

		const MapBiomesRun again = run(settings, glm::vec2(pixel));
		const bool deterministic = again.ids == gpuRun.ids && again.colors == gpuRun.colors && again.splat == gpuRun.splat;
		Report("mapbiomes: deterministic", deterministic ? "same result twice" : "results differ", deterministic);

		//Two biomes split by temperature: different temperature seeds give different maps (large texels, so the noise changes over the map)
		mapbiomes::Settings split = settings;
		split.config.table.resize(2);
		split.config.table[0].ranges = {};
		split.config.table[1].ranges = {};
		split.config.table[0].ranges[biome::TEMPERATURE] = biome::Range{ -1.0f, 0.0f };
		split.config.table[1].ranges[biome::TEMPERATURE] = biome::Range{ 0.0f, 1.0f };
		std::vector<std::vector<int>> maps;
		for (int seed = 1; seed <= 6; seed++) {
			split.config.temperature.seed = seed;
			maps.push_back(run(split, glm::vec2(500.0f)).ids);
		}
		const bool noiseOk = std::any_of(maps.begin() + 1, maps.end(), [&](const std::vector<int>& m) { return m != maps[0]; });
		Report("mapbiomes: temperature noise matters", noiseOk ? "seeds change the biomes" : "the same biomes for every seed", noiseOk);

		//Material patches are measured in texels like in TerrainGen, so a map of large pixels (a DEM) gets patches of the same size in texels;
		//the biomes here do not depend on temperature and humidity, so only the patches could change with the pixel size
		mapbiomes::Settings patchy = settings;
		patchy.config.materialPatchiness = 1.0f;
		const MapBiomesRun fine = run(patchy, glm::vec2(pixel)), coarse = run(patchy, glm::vec2(pixel) * 15.0f);
		size_t patchDiffer = 0;
		for (size_t i = 0; i < fine.splat.size(); i++) {
			patchDiffer += fine.splat[i] != coarse.splat[i] ? 1 : 0;
		}
		const bool patchesOk = fine.ids == coarse.ids && patchDiffer == 0;
		Report("mapbiomes: patches in texels", std::to_string(patchDiffer) + " splat bytes differ at 15x larger pixels", patchesOk);
		return idsOk && colorsOk && splatOk && deterministic && noiseOk && patchesOk && paintOk && namesOk && regionOk && coverOk && emptySame && displayOk
			&& paintCoverOk;
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
		//two passes = modified + biomes with the splat map, the way the maps with biomes were generated before GenerateWorld
		std::cout << "\nGPU time [ms]   terrain   modified   biomes   +splat   two passes   one pass   surface map\n";
		for (int size : { 512, 1024, 2048, 4096 }) {
			TextureClass height(size, size, GL_R32F), colors(size, size, GL_RGBA32F), ids(size, size, GL_R32I), surface(size, size, GL_RGBA32F);
			TextureClass splat(size, size, GL_RGBA8, GpuHeightGenerator::splatLayers);
			const double terrainMs = MedianMs([&] {
				gpu.GenerateTerrain(config, MapArea{}, height);
				glFinish();
			}, 9);
			//Terrain with the biome modifiers (all five noises and the biome table)
			const double modifiedMs = MedianMs([&] {
				gpu.GenerateTerrain(config, MapArea{}, height, &biomes);
				glFinish();
			}, 9);
			const double biomesMs = MedianMs([&] {
				gpu.GenerateBiomes(biomes, config, MapArea{}, colors, ids);
				glFinish();
			}, 9);
			const double splatMs = MedianMs([&] {
				gpu.GenerateBiomes(biomes, config, MapArea{}, colors, ids, &splat);
				glFinish();
			}, 9);
			const double twoPassMs = MedianMs([&] {
				gpu.GenerateTerrain(config, MapArea{}, height, &biomes);
				gpu.GenerateBiomes(biomes, config, MapArea{}, colors, ids, &splat);
				glFinish();
			}, 9);
			const double onePassMs = MedianMs([&] {
				gpu.GenerateWorld(config, biomes, MapArea{}, height, colors, ids, &splat);
				glFinish();
			}, 9);
			const double surfaceMs = MedianMs([&] {
				surfacePass.Run(height, surfaceTexelSize, surface);
				glFinish();
			}, 9);
			std::cout << std::left << std::setw(14) << (std::to_string(size) + "x" + std::to_string(size)) << std::right << std::fixed << std::setprecision(2)
				<< std::setw(10) << terrainMs << std::setw(11) << modifiedMs << std::setw(9) << biomesMs << std::setw(9) << splatMs
				<< std::setw(13) << twoPassMs << std::setw(11) << onePassMs << std::setw(14) << surfaceMs << std::defaultfloat << "\n" << std::left;
		}
		//The nested method against LAYERED, the tree made from the default LAYERED curves
		const TerrainConfig nestedConfig = NestedFromLayered(false);
		std::cout << "nested [ms]     terrain   (layered)   one pass   (layered)\n";
		for (int size : { 1024, 4096 }) {
			TextureClass height(size, size, GL_R32F), colors(size, size, GL_RGBA32F), ids(size, size, GL_R32I);
			TextureClass splat(size, size, GL_RGBA8, GpuHeightGenerator::splatLayers);
			const auto terrainMs = [&](const TerrainConfig& t) {
				return MedianMs([&] {
					gpu.GenerateTerrain(t, MapArea{}, height);
					glFinish();
				}, 9);
			};
			const auto onePassMs = [&](const TerrainConfig& t) {
				return MedianMs([&] {
					gpu.GenerateWorld(t, biomes, MapArea{}, height, colors, ids, &splat);
					glFinish();
				}, 9);
			};
			std::cout << std::left << std::setw(14) << (std::to_string(size) + "x" + std::to_string(size)) << std::right << std::fixed << std::setprecision(2)
				<< std::setw(10) << terrainMs(nestedConfig) << std::setw(12) << terrainMs(config)
				<< std::setw(11) << onePassMs(nestedConfig) << std::setw(12) << onePassMs(config) << std::defaultfloat << "\n" << std::left;
		}
		//What a sculpt frame pays for the surface map: the square of a 128-texel brush instead of the whole map
		TextureClass regionHeight(4096, 4096, GL_R32F), regionSurface(4096, 4096, GL_RGBA32F);
		gpu.GenerateTerrain(config, MapArea{}, regionHeight);
		const double regionMs = MedianMs([&] {
			surfacePass.RunRegion(regionHeight, surfaceTexelSize, regionSurface, glm::ivec4(1900, 1900, 257, 257));
			glFinish();
		}, 9);
		std::cout << "surface map of a 257x257 dab on a 4096x4096 map: " << std::fixed << std::setprecision(2) << regionMs << std::defaultfloat << " ms\n";
	}

	void RunErosionTimings(GpuHeightGenerator& gpu, GpuErosion& erosion)
	{
		std::cout << "\nerosion [ms]    droplets 20k   200k   1M (lifetime 64)   pipes (300 iterations)\n";
		for (int size : { 512, 1024, 2048 }) {
			TextureClass base(size, size, GL_R32F), eroded(size, size, GL_R32F);
			gpu.GenerateNoise(noise::NoiseConfigParameters{}, MapArea{}, base);
			std::cout << std::left << std::setw(16) << (std::to_string(size) + "x" + std::to_string(size)) << std::right << std::fixed << std::setprecision(2);
			uint32_t seed = 0;
			for (int count : { 20000, 200000, 1000000 }) {
				erosion::ErosionConfig config;
				config.dropletCount = count;
				const double ms = MedianMs([&] {
					erosion.Erode(config, base, eroded, seed++);
					glFinish();
				}, 3);
				std::cout << std::setw(count == 20000 ? 12 : 7) << ms;
			}
			erosion::ErosionConfig pipes;
			pipes.method = erosion::Method::PIPES;
			const double pipesMs = MedianMs([&] {
				erosion.Erode(pipes, base, eroded, 0);
				glFinish();
			}, 3);
			std::cout << std::setw(12) << pipesMs << std::defaultfloat << "\n" << std::left;
		}
	}
}

int RunGpuHeightCheck(bool timings)
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
		GpuErosion erosion;
		GpuSculpt sculptor;
		if (gpu.Initialize() && surfacePass.Initialize() && erosion.Initialize() && sculptor.Initialize()) {
			bool ok = CheckRanges(gpu);
			ok = CheckDeterminism(gpu) && ok;
			ok = CheckShapeOperations(gpu) && ok;
			ok = CheckLayered(gpu) && ok;
			ok = CheckNested(gpu) && ok;
			ok = CheckBiomes(gpu) && ok;
			ok = CheckClimate(gpu) && ok;
			ok = CheckModifiers(gpu) && ok;
			ok = CheckMaterials(gpu) && ok;
			ok = CheckWorld(gpu) && ok;
			ok = CheckLand(gpu) && ok;
			ok = CheckChunks(gpu) && ok;
			ok = CheckRivers(gpu) && ok;
			ok = CheckAsync(gpu) && ok;
			ok = CheckSurface(surfacePass) && ok;
			ok = CheckSurfaceRegion(surfacePass) && ok;
			ok = CheckErosion(gpu, erosion) && ok;
			ok = CheckSculpt(sculptor) && ok;
			ok = CheckMapBiomes(gpu) && ok;
			if (timings) {
				RunTimings(gpu, surfacePass);
				RunErosionTimings(gpu, erosion);
			}
			std::cout << "\n" << (ok ? "All checks passed" : "[ERROR] Some checks failed") << std::endl;
			result = ok ? 0 : 1;
		}
	}
	glfwDestroyWindow(window);
	glfwTerminate();
	return result;
}
