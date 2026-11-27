#include "doctest/doctest.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "NestedSpline.h"
#include "Preset.h"
#include "WorldConfig.h"

//Every field differs from its default, so a field left out of saving or loading fails the comparison
static noise::NoiseConfigParameters DistinctNoise(int base)
{
	noise::NoiseConfigParameters n;
	float b = static_cast<float>(base);
	n.seed = base;
	n.resolution = 300 + base;
	n.xoffset = b + 0.1f;
	n.yoffset = b + 0.2f;
	n.octaves = 3;
	n.scale = b + 0.3f;
	n.constrast = b + 0.4f;
	n.redistribution = b + 0.5f;
	n.lacunarity = b + 0.6f;
	n.persistance = b + 0.7f;
	n.option = noise::Options::FLATTEN_NEGATIVES;
	n.revertGain = b + 0.8f;
	n.Ridge = true;
	n.RidgeGain = b + 0.9f;
	n.RidgeOffset = b + 1.1f;
	n.island = true;
	n.mixPower = b + 1.2f;
	n.islandType = noise::IslandType::SQUIRCLE;
	n.symmetrical = true;
	n.warpStrength = b + 1.3f;
	n.warpScale = b + 1.4f;
	n.slopeErosion = b + 1.5f;
	n.terraceSteps = 5 + base;
	n.terraceSharpness = b + 1.6f;
	return n;
}

static void CheckSameNoise(const noise::NoiseConfigParameters& a, const noise::NoiseConfigParameters& b)
{
	CHECK(a.seed == b.seed);
	CHECK(a.resolution == b.resolution);
	CHECK(a.xoffset == b.xoffset);
	CHECK(a.yoffset == b.yoffset);
	CHECK(a.octaves == b.octaves);
	CHECK(a.scale == b.scale);
	CHECK(a.constrast == b.constrast);
	CHECK(a.redistribution == b.redistribution);
	CHECK(a.lacunarity == b.lacunarity);
	CHECK(a.persistance == b.persistance);
	CHECK(a.option == b.option);
	CHECK(a.revertGain == b.revertGain);
	CHECK(a.Ridge == b.Ridge);
	CHECK(a.RidgeGain == b.RidgeGain);
	CHECK(a.RidgeOffset == b.RidgeOffset);
	CHECK(a.island == b.island);
	CHECK(a.mixPower == b.mixPower);
	CHECK(a.islandType == b.islandType);
	CHECK(a.symmetrical == b.symmetrical);
	CHECK(a.warpStrength == b.warpStrength);
	CHECK(a.warpScale == b.warpScale);
	CHECK(a.slopeErosion == b.slopeErosion);
	CHECK(a.terraceSteps == b.terraceSteps);
	CHECK(a.terraceSharpness == b.terraceSharpness);
}

static void CheckSameSpline(const SplinePoints& a, const SplinePoints& b)
{
	CHECK(a.x == b.x);
	CHECK(a.y == b.y);
	CHECK(a.cubic == b.cubic);
}

//Three levels, curves over C, M and W, some linear, some cubic; values unlike any other field the invalid cases replace
static NestedCurve DistinctNestedTree()
{
	NestedCurve w{ { { -1.0, 0.125, {} }, { 0.25, 0.625, {} }, { 1.0, 0.875, {} } }, true };
	NestedCurve m{ { { -1.0, 0.25, {} }, { -0.5, 0.0, { w } }, { 0.5, 0.375, {} }, { 1.0, 0.75, {} } }, false };
	return NestedCurve{ { { -1.0, 0.0, {} }, { 0.25, 0.0, { m } }, { 1.0, 0.5, { w } } }, true };
}

static void CheckSameNestedCurve(const NestedCurve& a, const NestedCurve& b)
{
	CHECK(a.cubic == b.cubic);
	REQUIRE(a.points.size() == b.points.size());
	for (size_t i = 0; i < a.points.size(); i++) {
		CAPTURE(i);
		CHECK(a.points[i].x == b.points[i].x);
		REQUIRE(a.points[i].sub.size() == b.points[i].sub.size());
		if (a.points[i].sub.empty()) {
			CHECK(a.points[i].y == b.points[i].y);
		}
		else {
			CheckSameNestedCurve(a.points[i].sub[0], b.points[i].sub[0]);
		}
	}
}

//Every erosion field unlike the defaults
static erosion::ErosionConfig DistinctErosion()
{
	erosion::ErosionConfig e;
	e.erosionRate = 0.11f;
	e.depositionRate = 0.12f;
	e.evaporationRate = 0.13f;
	e.gravity = 0.14f;
	e.inertia = 0.15f;
	e.minSlope = 0.16f;
	e.erosionRadius = 7;
	e.blur = 0.17f;
	e.dropletLifetime = 8;
	e.dropletCount = 9;
	e.initialWater = 0.18f;
	e.initialVelocity = 0.19f;
	e.initialCapacity = 0.21f;
	e.method = erosion::Method::PIPES;
	erosion::PipeConfig& p = e.pipes;
	p.iterations = 11;
	p.timeStep = 0.22f;
	p.heightScale = 23.0f;
	p.rainRate = 0.24f;
	p.evaporationRate = 0.25f;
	p.sedimentCapacity = 0.26f;
	p.minTilt = 0.27f;
	p.dissolvingRate = 0.28f;
	p.depositionRate = 0.29f;
	p.talusAngle = 31.0f;
	p.thermalRate = 0.32f;
	return e;
}

static void CheckSameErosion(const erosion::ErosionConfig& l, const erosion::ErosionConfig& e)
{
	CHECK(l.erosionRate == e.erosionRate);
	CHECK(l.depositionRate == e.depositionRate);
	CHECK(l.evaporationRate == e.evaporationRate);
	CHECK(l.gravity == e.gravity);
	CHECK(l.inertia == e.inertia);
	CHECK(l.minSlope == e.minSlope);
	CHECK(l.erosionRadius == e.erosionRadius);
	CHECK(l.blur == e.blur);
	CHECK(l.dropletLifetime == e.dropletLifetime);
	CHECK(l.dropletCount == e.dropletCount);
	CHECK(l.initialWater == e.initialWater);
	CHECK(l.initialVelocity == e.initialVelocity);
	CHECK(l.initialCapacity == e.initialCapacity);
	CHECK(l.method == e.method);
	const erosion::PipeConfig& lp = l.pipes;
	const erosion::PipeConfig& p = e.pipes;
	CHECK(lp.iterations == p.iterations);
	CHECK(lp.timeStep == p.timeStep);
	CHECK(lp.heightScale == p.heightScale);
	CHECK(lp.rainRate == p.rainRate);
	CHECK(lp.evaporationRate == p.evaporationRate);
	CHECK(lp.sedimentCapacity == p.sedimentCapacity);
	CHECK(lp.minTilt == p.minTilt);
	CHECK(lp.dissolvingRate == p.dissolvingRate);
	CHECK(lp.depositionRate == p.depositionRate);
	CHECK(lp.talusAngle == p.talusAngle);
	CHECK(lp.thermalRate == p.thermalRate);
}

static WorldConfig DistinctWorld()
{
	WorldConfig c;
	c.width = 123;
	c.height = 45;
	c.terrain.method = EvaluationMethod::SPLINE_COMBINE;
	c.terrain.continentalness = DistinctNoise(1);
	c.terrain.mountainousness = DistinctNoise(2);
	c.terrain.weirdness = DistinctNoise(3);
	c.terrain.continentalnessSpline = { {-1.0, 0.1, 1.0}, {0.0, 0.2, 1.0}, true };
	c.terrain.mountainousnessSpline = { {-1.0, 0.3, 1.0}, {0.0, 0.4, 1.0}, false };
	c.terrain.weirdnessSpline = { {-1.0, 0.5, 1.0}, {1.0, 0.6, 0.0}, true };
	c.terrain.seaLevel = 0.42f;
	c.terrain.coastWidth = 0.07f;
	c.terrain.mountainShare = 0.33f;
	c.terrain.peaksSpline = { {-1.0, 0.7, 1.0}, {0.1, 0.8, 0.9}, true };
	c.terrain.nested.root = DistinctNestedTree();
	c.terrain.nested.seaLevel = 0.17f;
	c.biomes.temperature = DistinctNoise(4);
	c.biomes.humidity = DistinctNoise(5);
	c.biomes.blendWidth = 0.17f;
	c.biomes.climate = { 1.7f, -0.23f, 0.11f };
	c.biomes.modifyTerrain = false;
	c.biomes.terrainBlendWidth = 0.27f;
	c.biomes.detailSeed = 4321;
	c.biomes.materialBlendWidth = 0.19f;
	c.biomes.materialPatchiness = 0.37f;
	c.biomes.materialPatchContrast = 2.75f;
	c.biomes.materialPatchSize = 13.5f;
	//Unlike the default table: other count, names, colors, every range, every terrain modifier and material layer
	biome::Biome alpha{ "Alpha", glm::vec3(0.31f, 0.32f, 0.33f),
		{ biome::Range{ -0.41f, 0.42f }, biome::Range{ -0.43f, 0.44f }, biome::Range{ -0.45f, 0.46f }, biome::Range{ -0.47f, 0.48f }, biome::Range{ -0.49f, 0.51f } },
		biome::TerrainModifier{ 0.71f, 0.072f, 0.073f, 7.4f, true },
		{ { { 3, 0.25f }, { 7, 0.5f }, { 15, 0.125f } } },
		{ biome::Plant{ "Oak", 0.37f, 0.81f, 2.25f, 33.5f, biome::Range{ -0.11f, 0.62f }, 0.45f },
			biome::Plant{ "Pebble", 0.0625f, 1.5f, 1.75f, 71.0f, biome::Range{ 0.13f, 0.14f }, 0.0f } } };
	biome::Biome beta{ "Beta", glm::vec3(0.61f, 0.62f, 0.63f),
		{ biome::Range{ 0.52f, 0.53f }, biome::Range{ 0.54f, 0.55f }, biome::Range{ 0.56f, 0.57f }, biome::Range{ 0.58f, 0.59f }, biome::Range{ 0.64f, 0.65f } },
		biome::TerrainModifier{ 1.31f, -0.032f, 0.033f, 33.0f, false },
		{ { { 1, 2.5f } } } };
	c.biomes.table = { alpha, beta };
	c.rivers = { rivers::Method::NOISE, 41.5f, 0.0031f, 0.0017f, 0.35f, 77.5f, 5, 4096, 4, 123456.0f, 2.5f, 17.5f, 6.25f, 3, 0.0013f, 0.0055f, 4321.0f, 300.0f,
		1234.0f, 7.5f, 0.15f, 0.45f, 98765 };
	c.erosion = DistinctErosion();
	return c;
}

static void CheckSameRivers(const rivers::RiverConfig& a, const rivers::RiverConfig& b)
{
	CHECK(a.method == b.method);
	CHECK(a.bankWidth == b.bankWidth);
	CHECK(a.bankSlope == b.bankSlope);
	CHECK(a.depthPerWidth == b.depthPerWidth);
	CHECK(a.humidity == b.humidity);
	CHECK(a.humidityRange == b.humidityRange);
	CHECK(a.bedMaterial == b.bedMaterial);
	CHECK(a.regionSize == b.regionSize);
	CHECK(a.cellSize == b.cellSize);
	CHECK(a.sourceArea == b.sourceArea);
	CHECK(a.minHalfWidth == b.minHalfWidth);
	CHECK(a.maxHalfWidth == b.maxHalfWidth);
	CHECK(a.meander == b.meander);
	CHECK(a.smoothing == b.smoothing);
	CHECK(a.surfaceDrop == b.surfaceDrop);
	CHECK(a.minLakeDepth == b.minLakeDepth);
	CHECK(a.minLakeArea == b.minLakeArea);
	CHECK(a.borderFade == b.borderFade);
	CHECK(a.noiseScale == b.noiseScale);
	CHECK(a.noiseHalfWidth == b.noiseHalfWidth);
	CHECK(a.maxElevation == b.maxElevation);
	CHECK(a.noiseWarp == b.noiseWarp);
	CHECK(a.noiseSeed == b.noiseSeed);
}

static void CheckSameBiomes(const std::vector<biome::Biome>& a, const std::vector<biome::Biome>& b)
{
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); i++) {
		CAPTURE(i);
		CHECK(a[i].name == b[i].name);
		CHECK(a[i].color == b[i].color);
		for (int p = 0; p < biome::PARAMETER_COUNT; p++) {
			CHECK(a[i].ranges[p].min == b[i].ranges[p].min);
			CHECK(a[i].ranges[p].max == b[i].ranges[p].max);
		}
		CHECK(a[i].terrain.amplitude == b[i].terrain.amplitude);
		CHECK(a[i].terrain.offset == b[i].terrain.offset);
		CHECK(a[i].terrain.detailAmplitude == b[i].terrain.detailAmplitude);
		CHECK(a[i].terrain.detailSize == b[i].terrain.detailSize);
		CHECK(a[i].terrain.detailRidge == b[i].terrain.detailRidge);
		//Unused layers (weight 0) are not saved, so only the used ones have to match
		for (int l = 0; l < biome::materialLayers; l++) {
			CAPTURE(l);
			CHECK(a[i].materials[l].weight == b[i].materials[l].weight);
			if (a[i].materials[l].weight > 0.0f) {
				CHECK(a[i].materials[l].material == b[i].materials[l].material);
			}
		}
		CHECK(a[i].plants == b[i].plants);
	}
}

//Replaces the first occurrence of from, which has to exist
static std::string Replaced(std::string text, const std::string& from, const std::string& to)
{
	size_t position = text.find(from);
	REQUIRE(position != std::string::npos);
	return text.replace(position, from.size(), to);
}

TEST_CASE("A world preset keeps every field")
{
	const WorldConfig original = DistinctWorld();
	WorldConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(original), loaded, error), error);

	CHECK(loaded.width == original.width);
	CHECK(loaded.height == original.height);
	CHECK(loaded.terrain.method == original.terrain.method);
	CheckSameNoise(loaded.terrain.continentalness, original.terrain.continentalness);
	CheckSameNoise(loaded.terrain.mountainousness, original.terrain.mountainousness);
	CheckSameNoise(loaded.terrain.weirdness, original.terrain.weirdness);
	CheckSameSpline(loaded.terrain.continentalnessSpline, original.terrain.continentalnessSpline);
	CheckSameSpline(loaded.terrain.mountainousnessSpline, original.terrain.mountainousnessSpline);
	CheckSameSpline(loaded.terrain.weirdnessSpline, original.terrain.weirdnessSpline);
	CHECK(loaded.terrain.seaLevel == original.terrain.seaLevel);
	CHECK(loaded.terrain.coastWidth == original.terrain.coastWidth);
	CHECK(loaded.terrain.mountainShare == original.terrain.mountainShare);
	CheckSameSpline(loaded.terrain.peaksSpline, original.terrain.peaksSpline);
	CheckSameNestedCurve(loaded.terrain.nested.root, original.terrain.nested.root);
	CHECK(loaded.terrain.nested.seaLevel == original.terrain.nested.seaLevel);
	CheckSameNoise(loaded.biomes.temperature, original.biomes.temperature);
	CheckSameNoise(loaded.biomes.humidity, original.biomes.humidity);
	CHECK(loaded.biomes.blendWidth == original.biomes.blendWidth);
	CHECK(loaded.biomes.climate.lapseRate == original.biomes.climate.lapseRate);
	CHECK(loaded.biomes.climate.coastHumidity == original.biomes.climate.coastHumidity);
	CHECK(loaded.biomes.climate.coastRange == original.biomes.climate.coastRange);
	CHECK(loaded.biomes.modifyTerrain == original.biomes.modifyTerrain);
	CHECK(loaded.biomes.terrainBlendWidth == original.biomes.terrainBlendWidth);
	CHECK(loaded.biomes.detailSeed == original.biomes.detailSeed);
	CHECK(loaded.biomes.materialBlendWidth == original.biomes.materialBlendWidth);
	CHECK(loaded.biomes.materialPatchiness == original.biomes.materialPatchiness);
	CHECK(loaded.biomes.materialPatchContrast == original.biomes.materialPatchContrast);
	CHECK(loaded.biomes.materialPatchSize == original.biomes.materialPatchSize);
	CheckSameBiomes(loaded.biomes.table, original.biomes.table);
	CheckSameRivers(loaded.rivers, original.rivers);
	CheckSameErosion(loaded.erosion, original.erosion);
}

TEST_CASE("Biome config survives its own JSON")
{
	const BiomeConfig original = DistinctWorld().biomes;
	BiomeConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::BiomesFromJson(preset::BiomesJson(original), loaded, error), error);
	CheckSameNoise(loaded.temperature, original.temperature);
	CheckSameNoise(loaded.humidity, original.humidity);
	CHECK(loaded.blendWidth == original.blendWidth);
	CHECK(loaded.climate.lapseRate == original.climate.lapseRate);
	CHECK(loaded.climate.coastHumidity == original.climate.coastHumidity);
	CHECK(loaded.climate.coastRange == original.climate.coastRange);
	CHECK(loaded.modifyTerrain == original.modifyTerrain);
	CHECK(loaded.terrainBlendWidth == original.terrainBlendWidth);
	CHECK(loaded.detailSeed == original.detailSeed);
	CHECK(loaded.materialBlendWidth == original.materialBlendWidth);
	CHECK(loaded.materialPatchiness == original.materialPatchiness);
	CHECK(loaded.materialPatchContrast == original.materialPatchContrast);
	CHECK(loaded.materialPatchSize == original.materialPatchSize);
	CheckSameBiomes(loaded.table, original.table);
}

TEST_CASE("Biome config JSON rejects an empty table")
{
	BiomeConfig loaded;
	loaded.blendWidth = 0.123f;
	std::string error;
	CHECK_FALSE(preset::BiomesFromJson(R"({"table": []})", loaded, error));
	CHECK_FALSE(error.empty());
	CHECK(loaded.blendWidth == 0.123f);
	CHECK_FALSE(preset::BiomesFromJson("not json", loaded, error));
}

TEST_CASE("A world preset without erosion gets the TerrainGen defaults")
{
	WorldConfig loaded;
	loaded.erosion = DistinctErosion();
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 2, "width": 64, "height": 32})", loaded, error), error);
	CHECK(loaded.erosion.pipes.heightScale == 256.0f);
	CHECK(loaded.erosion.method == erosion::Method::DROPLETS);
	CHECK(loaded.erosion.dropletCount == erosion::ErosionConfig().dropletCount);
}

TEST_CASE("Every river method survives saving, and a preset without rivers has none")
{
	for (rivers::Method method : { rivers::Method::NONE, rivers::Method::REGION, rivers::Method::NOISE }) {
		WorldConfig original = DistinctWorld();
		original.rivers.method = method;
		WorldConfig loaded;
		std::string error;
		REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(original), loaded, error), error);
		CHECK(loaded.rivers.method == method);
	}
	WorldConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 2, "width": 64, "height": 32})", loaded, error), error);
	CheckSameRivers(loaded.rivers, rivers::RiverConfig{});
}

TEST_CASE("Every terrain method survives saving")
{
	for (EvaluationMethod method : { EvaluationMethod::LINEAR_COMBINE, EvaluationMethod::SPLINE_COMBINE, EvaluationMethod::LAYERED,
		EvaluationMethod::NESTED }) {
		WorldConfig original = DistinctWorld();
		original.terrain.method = method;
		WorldConfig loaded;
		std::string error;
		REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(original), loaded, error), error);
		CHECK(loaded.terrain.method == method);
	}
}

TEST_CASE("Nested tree round trip")
{
	WorldConfig original = DistinctWorld();
	original.terrain.method = EvaluationMethod::NESTED;
	WorldConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(original), loaded, error), error);
	CHECK(loaded.terrain.method == EvaluationMethod::NESTED);
	CheckSameNestedCurve(loaded.terrain.nested.root, original.terrain.nested.root);
	CHECK(loaded.terrain.nested.seaLevel == 0.17f);
	//An empty tree of another method stays empty
	original.terrain.method = EvaluationMethod::LAYERED;
	original.terrain.nested.root = {};
	REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(original), loaded, error), error);
	CHECK(loaded.terrain.nested.root.points.empty());
}

TEST_CASE("NESTED without a tree is filled from LAYERED on load")
{
	WorldConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 2, "width": 64, "height": 32,
		"terrain": {"method": "NESTED", "seaLevel": 0.4, "mountainShare": 0.5}})", loaded, error), error);
	REQUIRE(loaded.terrain.method == EvaluationMethod::NESTED);
	const NestedTerrain expected = nested::FromLayered(loaded.terrain);
	REQUIRE(!loaded.terrain.nested.root.points.empty());
	CheckSameNestedCurve(loaded.terrain.nested.root, expected.root);
	CHECK(loaded.terrain.nested.seaLevel == doctest::Approx(0.2f));

	//A sea level written in the file is kept
	REQUIRE_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 2, "width": 64, "height": 32,
		"terrain": {"method": "NESTED", "nested": {"seaLevel": 0.35}}})", loaded, error), error);
	CHECK(!loaded.terrain.nested.root.points.empty());
	CHECK(loaded.terrain.nested.seaLevel == 0.35f);
}

TEST_CASE("Broken nested trees are rejected")
{
	WorldConfig original = DistinctWorld();
	original.terrain.method = EvaluationMethod::NESTED;
	const std::string valid = preset::ToJson(original);
	//Presets are written with an indent of 2, so every point is an object over several lines
	const std::string cases[] = {
		Replaced(valid, "\"seaLevel\": 0.17", "\"seaLevel\": 1.5"),
		//A value of the W-curve out of range
		Replaced(valid, "\"y\": 0.125", "\"y\": 1.125"),
		//The second point of the root at -1, like the first one
		Replaced(valid, "\"x\": 0.25", "\"x\": -1.0"),
		Replaced(valid, "\"x\": 0.25", "\"x\": \"a\""),
		Replaced(valid, "\"points\": [", "\"points\": 1, \"unused\": ["),
		Replaced(valid, "\"curve\": {", "\"curve\": 1, \"unused\": {"),
	};
	for (const std::string& text : cases) {
		CAPTURE(text);
		WorldConfig config;
		config.width = 77;
		std::string error;
		CHECK_FALSE(preset::FromJson(text, config, error));
		CHECK(error.find("nested") != std::string::npos);
		CHECK(config.width == 77);
	}
}

TEST_CASE("A noise preset keeps every field")
{
	NoiseMapConfig original;
	original.width = 123;
	original.height = 45;
	original.noise = DistinctNoise(1);
	original.erosion = DistinctErosion();

	NoiseMapConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(original), loaded, error), error);

	CHECK(loaded.width == original.width);
	CHECK(loaded.height == original.height);
	CheckSameNoise(loaded.noise, original.noise);
	CheckSameErosion(loaded.erosion, original.erosion);
}

TEST_CASE("A noise preset without the erosion method uses droplets")
{
	NoiseMapConfig original;
	original.width = 64;
	original.height = 32;
	original.erosion.method = erosion::Method::DROPLETS;
	std::string text = preset::ToJson(original);
	const size_t method = text.find("\"method\": \"DROPLETS\",");
	REQUIRE(method != std::string::npos);
	text.erase(method, std::string("\"method\": \"DROPLETS\",").size());

	NoiseMapConfig loaded;
	loaded.erosion.method = erosion::Method::PIPES;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(text, loaded, error), error);
	CHECK(loaded.erosion.method == erosion::Method::DROPLETS);
}

TEST_CASE("Floats survive saving exactly and are written in a short form")
{
	NoiseMapConfig config;
	config.width = 16;
	config.height = 16;

	std::mt19937 random(7);
	std::uniform_real_distribution<float> values(-10.0f, 10.0f);
	int changed = 0;
	for (int i = 0; i < 2000; i++) {
		config.noise.scale = values(random);
		NoiseMapConfig loaded;
		std::string error;
		REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(config), loaded, error), error);
		changed += loaded.noise.scale != config.noise.scale;
	}
	CHECK(changed == 0);

	config.noise.scale = 0.3f;
	CHECK(preset::ToJson(config).find("\"scale\": 0.3,") != std::string::npos);
}

TEST_CASE("A field missing from a preset keeps its default value")
{
	WorldConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 2, "width": 64, "height": 32})", loaded, error), error);

	const WorldConfig defaults;
	CHECK(loaded.width == 64);
	CHECK(loaded.height == 32);
	CheckSameNoise(loaded.terrain.continentalness, defaults.terrain.continentalness);
	CheckSameSpline(loaded.terrain.weirdnessSpline, defaults.terrain.weirdnessSpline);
	CHECK(loaded.biomes.blendWidth == defaults.biomes.blendWidth);
	CHECK(loaded.biomes.climate.lapseRate == defaults.biomes.climate.lapseRate);
	CHECK(loaded.biomes.climate.coastRange == defaults.biomes.climate.coastRange);
	CheckSameBiomes(loaded.biomes.table, defaults.biomes.table);
}

TEST_CASE("A biome missing fields in a preset keeps the values of a new biome")
{
	WorldConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 2, "width": 64, "height": 32,
		"biomes": {"table": [{"name": "Only", "humidity": [0.1, 0.2]}]}})", loaded, error), error);

	REQUIRE(loaded.biomes.table.size() == 1);
	const biome::Biome& b = loaded.biomes.table[0];
	CHECK(b.name == "Only");
	CHECK(b.color == glm::vec3(0.0f));
	CHECK(b.ranges[biome::HUMIDITY].min == 0.1f);
	CHECK(b.ranges[biome::HUMIDITY].max == 0.2f);
	CHECK(b.ranges[biome::TEMPERATURE].min == -1.0f);
	CHECK(b.ranges[biome::TEMPERATURE].max == 1.0f);
	CHECK(b.materials[0].material == 0);
	CHECK(b.materials[0].weight == 1.0f);
	//No default biome has this name: no plants
	CHECK(b.plants.empty());
	CHECK(loaded.biomes.materialPatchSize == BiomeConfig().materialPatchSize);
}

TEST_CASE("A biome saved before the plants existed gets the plants of the default biome of its name")
{
	WorldConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 2, "width": 64, "height": 32,
		"biomes": {"table": [{"name": "Forest"}, {"name": "Bare", "plants": []}, {"name": "Taiga", "plants": [{"model": "Fir"}]}]}})", loaded, error), error);
	REQUIRE(loaded.biomes.table.size() == 3);
	CHECK(loaded.biomes.table[0].plants == biome::DefaultPlants("Forest"));
	CHECK_FALSE(loaded.biomes.table[0].plants.empty());
	CHECK(loaded.biomes.table[1].plants.empty());
	//Fields missing in a plant keep the values of a new Plant
	REQUIRE(loaded.biomes.table[2].plants.size() == 1);
	biome::Plant expected;
	expected.model = "Fir";
	CHECK(loaded.biomes.table[2].plants[0] == expected);
}

TEST_CASE("Unknown fields are ignored")
{
	WorldConfig loaded;
	std::string error;
	CHECK_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 2, "width": 64, "height": 32, "fromTheFuture": [1]})", loaded, error), error);
}

TEST_CASE("A preset keeps the map offset when given one, and has none otherwise")
{
	std::string error;
	SUBCASE("World")
	{
		WorldConfig loaded;
		std::optional<glm::ivec2> offset = glm::ivec2(5, 5);
		REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(DistinctWorld(), glm::ivec2(-1536, 2047)), loaded, offset, error), error);
		REQUIRE(offset.has_value());
		CHECK(*offset == glm::ivec2(-1536, 2047));
		CHECK(loaded.width == DistinctWorld().width);

		REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(DistinctWorld()), loaded, offset, error), error);
		CHECK_FALSE(offset.has_value());
	}
	SUBCASE("Noise")
	{
		NoiseMapConfig config;
		config.width = 64;
		config.height = 32;
		NoiseMapConfig loaded;
		std::optional<glm::ivec2> offset;
		REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(config, glm::ivec2(preset::maxMapOffset, -preset::maxMapOffset)), loaded, offset, error), error);
		REQUIRE(offset.has_value());
		CHECK(*offset == glm::ivec2(preset::maxMapOffset, -preset::maxMapOffset));
	}
	SUBCASE("Reading without asking for the offset ignores it")
	{
		WorldConfig loaded;
		CHECK_MESSAGE(preset::FromJson(preset::ToJson(DistinctWorld(), glm::ivec2(3, 4)), loaded, error), error);
	}
}

TEST_CASE("An invalid map offset rejects the preset and leaves the offset unchanged")
{
	const std::string valid = preset::ToJson(DistinctWorld(), glm::ivec2(3, 4));
	const std::string written = "\"mapOffset\": [\n    3,\n    4\n  ]";
	REQUIRE(valid.find(written) != std::string::npos);
	const char* invalid[] = {
		"\"mapOffset\": [3]",
		"\"mapOffset\": [3, 4, 5]",
		"\"mapOffset\": [3.5, 4]",
		"\"mapOffset\": \"3, 4\"",
		"\"mapOffset\": [16777217, 0]",
		"\"mapOffset\": [0, -16777217]",
	};
	for (const std::string field : invalid) {
		CAPTURE(field);
		WorldConfig loaded;
		std::optional<glm::ivec2> offset = glm::ivec2(9, 9);
		std::string error;
		CHECK_FALSE(preset::FromJson(Replaced(valid, written, field), loaded, offset, error));
		CHECK(error.find("mapOffset") != std::string::npos);
		REQUIRE(offset.has_value());
		CHECK(*offset == glm::ivec2(9, 9));
	}
}

TEST_CASE("Invalid presets are rejected and the config stays unchanged")
{
	const std::string valid = preset::ToJson(DistinctWorld());

	struct Case {
		const char* name;
		std::string text;
	};
	const Case cases[] = {
		{ "not JSON", "{ not json" },
		{ "not an object", "[1, 2]" },
		{ "no kind", R"({"version": 2, "width": 64, "height": 32})" },
		{ "preset of the other mode", R"({"kind": "noise", "version": 2, "width": 64, "height": 32})" },
		{ "no version", R"({"kind": "world", "width": 64, "height": 32})" },
		{ "newer version", Replaced(valid, "\"version\": 2", "\"version\": 3") },
		{ "version 1 with biome levels", Replaced(valid, "\"version\": 2", "\"version\": 1") },
		{ "no size", R"({"kind": "world", "version": 2})" },
		{ "size too small", Replaced(valid, "\"width\": 123", "\"width\": 1") },
		{ "size too large", Replaced(valid, "\"width\": 123", "\"width\": 100000") },
		{ "fractional integer", Replaced(valid, "\"width\": 123", "\"width\": 123.5") },
		{ "integer out of range", Replaced(valid, "\"width\": 123", "\"width\": 99999999999") },
		{ "wrong type", Replaced(valid, "\"width\": 123", "\"width\": \"wide\"") },
		{ "wrong section type", Replaced(valid, "\"terrain\": {", "\"terrain\": 1, \"unused\": {") },
		{ "unknown enum", Replaced(valid, "\"FLATTEN_NEGATIVES\"", "\"SOMETHING\"") },
		{ "enum as a number", Replaced(valid, "\"FLATTEN_NEGATIVES\"", "1") },
		{ "method without UI", Replaced(valid, "\"SPLINE_COMBINE\"", "\"C\"") },
		{ "float out of range", Replaced(valid, "\"scale\": 1.3", "\"scale\": 1e300") },
		{ "zero octaves", Replaced(valid, "\"octaves\": 3", "\"octaves\": 0") },
		{ "zero resolution", Replaced(valid, "\"resolution\": 301", "\"resolution\": 0") },
		{ "sea level above 1", Replaced(valid, "\"seaLevel\": 0.42", "\"seaLevel\": 1.5") },
		{ "zero coast width", Replaced(valid, "\"coastWidth\": 0.07", "\"coastWidth\": 0.0") },
		{ "negative mountain share", Replaced(valid, "\"mountainShare\": 0.33", "\"mountainShare\": -0.1") },
		{ "peaks spline y out of range", Replaced(valid, "0.8,", "1.8,") },
		{ "negative slope erosion", Replaced(valid, "\"slopeErosion\": 2.5", "\"slopeErosion\": -1.0") },
		{ "negative terrace steps", Replaced(valid, "\"terraceSteps\": 6", "\"terraceSteps\": -1") },
		{ "too many terrace steps", Replaced(valid, "\"terraceSteps\": 6", "\"terraceSteps\": 1000") },
		{ "spline sizes differ", Replaced(valid, "0.2,", "") },
		{ "spline x not ascending", Replaced(valid, "0.1,", "-1.0,") },
		{ "spline y out of range", Replaced(valid, "0.2,", "2.0,") },
		{ "negative blend width", Replaced(valid, "\"blendWidth\": 0.17", "\"blendWidth\": -0.17") },
		{ "zero coast range", Replaced(valid, "\"coastRange\": 0.11", "\"coastRange\": 0") },
		{ "climate not an object", Replaced(valid, "\"climate\": {", "\"climate\": 1, \"unused\": {") },
		{ "zero terrain blend width", Replaced(valid, "\"terrainBlendWidth\": 0.27", "\"terrainBlendWidth\": 0") },
		{ "zero detail size", Replaced(valid, "\"detailSize\": 7.4", "\"detailSize\": 0") },
		{ "detail ridge not a bool", Replaced(valid, "\"detailRidge\": true", "\"detailRidge\": 1") },
		{ "empty biome table", Replaced(valid, "\"table\": [", "\"table\": [], \"unused\": [") },
		{ "biome table not an array", Replaced(valid, "\"table\": [", "\"table\": 1, \"unused\": [") },
		{ "reversed biome range", Replaced(valid, "-0.41", "0.9") },
		{ "biome range of one number", Replaced(valid, "-0.41,", "") },
		{ "biome color out of range", Replaced(valid, "0.31", "1.31") },
		{ "biome name not a string", Replaced(valid, "\"Alpha\"", "7") },
		{ "zero material blend width", Replaced(valid, "\"materialBlendWidth\": 0.19", "\"materialBlendWidth\": 0") },
		{ "patchiness above 1", Replaced(valid, "\"materialPatchiness\": 0.37", "\"materialPatchiness\": 1.5") },
		{ "patch contrast below 1", Replaced(valid, "\"materialPatchContrast\": 2.75", "\"materialPatchContrast\": 0.5") },
		{ "zero patch size",Replaced(valid, "\"materialPatchSize\": 13.5", "\"materialPatchSize\": 0") },
		{ "material out of range", Replaced(valid, "\"material\": 15", "\"material\": 16") },
		{ "negative material weight", Replaced(valid, "\"weight\": 0.5", "\"weight\": -0.5") },
		{ "no material weight", Replaced(valid, "\"weight\": 2.5", "\"weight\": 0") },
		{ "materials not an array", Replaced(valid, "\"materials\": [", "\"materials\": 1, \"unused\": [") },
		{ "too many materials", Replaced(valid, "\"materials\": [", "\"materials\": [{}, {}, {}, {}, {},") },
		{ "plants not an array", Replaced(valid, "\"plants\": [", "\"plants\": 1, \"unused\": [") },
		{ "too many plants", Replaced(valid, "\"plants\": [", "\"plants\": [{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},") },
		{ "plant model not a string", Replaced(valid, "\"Oak\"", "3") },
		{ "plant density above 1", Replaced(valid, "\"density\": 0.37", "\"density\": 1.5") },
		{ "negative plant density", Replaced(valid, "\"density\": 0.37", "\"density\": -0.1") },
		{ "zero plant scale", Replaced(valid, "0.81,", "0.0,") },
		{ "reversed plant scale", Replaced(valid, "0.81,", "3.0,") },
		{ "plant scale of one number", Replaced(valid, "0.81,", "") },
		{ "plant slope above 90", Replaced(valid, "\"maxSlope\": 33.5", "\"maxSlope\": 91") },
		{ "reversed plant elevation", Replaced(valid, "-0.11", "0.9") },
		{ "plant clustering above 1", Replaced(valid, "\"clustering\": 0.45", "\"clustering\": 1.5") },
		{ "unknown river method", Replaced(valid, "\"NOISE\"", "\"LAVA\"") },
		{ "zero river bank width", Replaced(valid, "\"bankWidth\": 41.5", "\"bankWidth\": 0") },
		{ "river region of too few cells", Replaced(valid, "\"regionSize\": 4096", "\"regionSize\": 32") },
		{ "river region of too many cells", Replaced(valid, "\"regionSize\": 4096", "\"regionSize\": 40000") },
		{ "zero river cell size", Replaced(valid, "\"cellSize\": 4", "\"cellSize\": 0") },
		{ "zero river source area", Replaced(valid, "\"sourceArea\": 123456", "\"sourceArea\": 0") },
		{ "reversed river half width", Replaced(valid, "\"maxHalfWidth\": 17.5", "\"maxHalfWidth\": 1") },
		{ "negative river smoothing", Replaced(valid, "\"smoothing\": 3", "\"smoothing\": -1") },
		{ "zero river noise scale", Replaced(valid, "\"noiseScale\": 1234", "\"noiseScale\": 0") },
		{ "river bed material out of range", Replaced(valid, "\"bedMaterial\": 5", "\"bedMaterial\": 16") },
		{ "zero river humidity range", Replaced(valid, "\"humidityRange\": 77.5", "\"humidityRange\": 0") },
	};

	for (const Case& c : cases) {
		CAPTURE(c.name);
		WorldConfig config;
		config.width = 77;
		std::string error;
		CHECK_FALSE(preset::FromJson(c.text, config, error));
		CHECK_FALSE(error.empty());
		CHECK(config.width == 77);
	}
}

TEST_CASE("Preset names are safe as file names")
{
	CHECK(preset::IsValidName("Alps 2-final_v3"));

	CHECK_FALSE(preset::IsValidName(""));
	CHECK_FALSE(preset::IsValidName(" leading"));
	CHECK_FALSE(preset::IsValidName("trailing "));
	CHECK_FALSE(preset::IsValidName("../outside"));
	CHECK_FALSE(preset::IsValidName("a\\b"));
	CHECK_FALSE(preset::IsValidName("a.b"));
	CHECK_FALSE(preset::IsValidName("c:name"));
	CHECK_FALSE(preset::IsValidName(std::string(65, 'a')));
}

TEST_CASE("Presets are saved to and loaded from files")
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_preset_tests";
	std::filesystem::remove_all(folder);
	const std::filesystem::path path = folder / "nested" / "world.json";
	std::string error;

	CHECK(preset::List(folder / "nested").empty());

	WorldConfig original = DistinctWorld();
	REQUIRE_MESSAGE(preset::Save(path, original, error), error);

	WorldConfig loaded;
	REQUIRE_MESSAGE(preset::Load(path, loaded, error), error);
	CHECK(loaded.width == original.width);

	SUBCASE("Saving replaces the file and leaves no temporary file")
	{
		original.width = 200;
		REQUIRE_MESSAGE(preset::Save(path, original, error), error);
		REQUIRE_MESSAGE(preset::Load(path, loaded, error), error);
		CHECK(loaded.width == 200);
		CHECK(preset::List(folder / "nested").size() == 1);
		CHECK_FALSE(std::filesystem::exists(folder / "nested" / "world.json.tmp"));
	}
	SUBCASE("An invalid config is not saved and the file stays")
	{
		WorldConfig invalid = original;
		invalid.width = 0;
		CHECK_FALSE(preset::Save(path, invalid, error));
		CHECK_FALSE(error.empty());
		REQUIRE_MESSAGE(preset::Load(path, loaded, error), error);
		CHECK(loaded.width == original.width);
	}
	SUBCASE("Listing returns only JSON files, sorted")
	{
		WorldConfig other = original;
		REQUIRE_MESSAGE(preset::Save(folder / "nested" / "another.json", other, error), error);
		std::ofstream(folder / "nested" / "notes.txt") << "not a preset";
		auto files = preset::List(folder / "nested");
		REQUIRE(files.size() == 2);
		CHECK(files[0].filename() == "another.json");
		CHECK(files[1].filename() == "world.json");
	}
	SUBCASE("The map offset is saved and loaded; an offset out of range is not saved")
	{
		REQUIRE_MESSAGE(preset::Save(path, original, glm::ivec2(-7, 11), error), error);
		std::optional<glm::ivec2> offset;
		REQUIRE_MESSAGE(preset::Load(path, loaded, offset, error), error);
		REQUIRE(offset.has_value());
		CHECK(*offset == glm::ivec2(-7, 11));

		CHECK_FALSE(preset::Save(path, original, glm::ivec2(preset::maxMapOffset + 1, 0), error));
		REQUIRE_MESSAGE(preset::Load(path, loaded, offset, error), error);
		CHECK(*offset == glm::ivec2(-7, 11));
	}
	SUBCASE("Loading a missing file fails")
	{
		CHECK_FALSE(preset::Load(folder / "missing.json", loaded, error));
		CHECK_FALSE(error.empty());
	}

	std::filesystem::remove_all(folder);
}

TEST_CASE("Rivers JSON round trip")
{
	const rivers::RiverConfig original = DistinctWorld().rivers;
	rivers::RiverConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::RiversFromJson(preset::RiversJson(original), loaded, error), error);
	CheckSameRivers(loaded, original);

	rivers::RiverConfig untouched;
	CHECK_FALSE(preset::RiversFromJson("{ not json", untouched, error));
	CHECK_FALSE(error.empty());
	error.clear();
	CHECK_FALSE(preset::RiversFromJson(R"({"cellSize": 0})", untouched, error));
	CHECK_FALSE(error.empty());
	error.clear();
	CHECK_FALSE(preset::RiversFromJson(R"({"minLakeArea": -1})", untouched, error));
	CHECK_FALSE(error.empty());
	CheckSameRivers(untouched, rivers::RiverConfig{});
}
