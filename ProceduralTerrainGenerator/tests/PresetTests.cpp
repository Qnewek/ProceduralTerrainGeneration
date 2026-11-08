#include "doctest/doctest.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

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
	c.biomes.temperature = DistinctNoise(4);
	c.biomes.humidity = DistinctNoise(5);
	c.biomes.temperatureLevels = { -1.0f, 0.11f, 1.01f };
	c.biomes.humidityLevels = { -1.0f, 0.12f, 1.01f };
	c.biomes.continentalnessLevels = { -1.0f, 0.13f, 1.01f };
	c.biomes.mountainousnessLevels = { -1.0f, 0.14f, 1.01f };
	c.biomes.weirdnessLevels = { -1.0f, 0.15f, 1.01f };
	return c;
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
	CheckSameNoise(loaded.biomes.temperature, original.biomes.temperature);
	CheckSameNoise(loaded.biomes.humidity, original.biomes.humidity);
	CHECK(loaded.biomes.temperatureLevels == original.biomes.temperatureLevels);
	CHECK(loaded.biomes.humidityLevels == original.biomes.humidityLevels);
	CHECK(loaded.biomes.continentalnessLevels == original.biomes.continentalnessLevels);
	CHECK(loaded.biomes.mountainousnessLevels == original.biomes.mountainousnessLevels);
	CHECK(loaded.biomes.weirdnessLevels == original.biomes.weirdnessLevels);
}

TEST_CASE("A noise preset keeps every field")
{
	NoiseMapConfig original;
	original.width = 123;
	original.height = 45;
	original.noise = DistinctNoise(1);
	erosion::ErosionConfig& e = original.erosion;
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

	NoiseMapConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(original), loaded, error), error);

	CHECK(loaded.width == original.width);
	CHECK(loaded.height == original.height);
	CheckSameNoise(loaded.noise, original.noise);
	const erosion::ErosionConfig& l = loaded.erosion;
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
	REQUIRE_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 1, "width": 64, "height": 32})", loaded, error), error);

	const WorldConfig defaults;
	CHECK(loaded.width == 64);
	CHECK(loaded.height == 32);
	CheckSameNoise(loaded.terrain.continentalness, defaults.terrain.continentalness);
	CheckSameSpline(loaded.terrain.weirdnessSpline, defaults.terrain.weirdnessSpline);
	CHECK(loaded.biomes.temperatureLevels == defaults.biomes.temperatureLevels);
}

TEST_CASE("Unknown fields are ignored")
{
	WorldConfig loaded;
	std::string error;
	CHECK_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 1, "width": 64, "height": 32, "fromTheFuture": [1]})", loaded, error), error);
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
		{ "no kind", R"({"version": 1, "width": 64, "height": 32})" },
		{ "preset of the other mode", R"({"kind": "noise", "version": 1, "width": 64, "height": 32})" },
		{ "no version", R"({"kind": "world", "width": 64, "height": 32})" },
		{ "newer version", Replaced(valid, "\"version\": 1", "\"version\": 2") },
		{ "no size", R"({"kind": "world", "version": 1})" },
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
		{ "negative slope erosion", Replaced(valid, "\"slopeErosion\": 2.5", "\"slopeErosion\": -1.0") },
		{ "negative terrace steps", Replaced(valid, "\"terraceSteps\": 6", "\"terraceSteps\": -1") },
		{ "too many terrace steps", Replaced(valid, "\"terraceSteps\": 6", "\"terraceSteps\": 1000") },
		{ "spline sizes differ", Replaced(valid, "0.2,", "") },
		{ "spline x not ascending", Replaced(valid, "0.1,", "-1.0,") },
		{ "spline y out of range", Replaced(valid, "0.2,", "2.0,") },
		{ "levels not ascending", Replaced(valid, "0.11,", "-2.0,") },
		{ "levels not covering the range", Replaced(valid, "1.01", "0.9") },
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
	SUBCASE("Loading a missing file fails")
	{
		CHECK_FALSE(preset::Load(folder / "missing.json", loaded, error));
		CHECK_FALSE(error.empty());
	}

	std::filesystem::remove_all(folder);
}
