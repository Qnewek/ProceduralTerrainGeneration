#include "doctest/doctest.h"

#include <chrono>
#include <cmath>
#include <string>

#include "glm/gtc/packing.hpp"

#include "MapBiomes.h"
#include "Preset.h"
#include "WorldConfig.h"

namespace {
	mapbiomes::Settings DistinctSettings()
	{
		mapbiomes::Settings s;
		s.enabled = true;
		s.heightUnitMetres = 1234.5f;
		s.baseMetres = -321.25f;
		s.coastMetres = 7777.0f;
		s.reliefRadiusMetres = 555.5f;
		s.reliefMetres = 444.25f;
		s.climateOrigin = glm::vec2(1024.0f, -2048.5f);
		s.config.blendWidth = 0.17f;
		s.config.temperature.seed = 9876;
		s.config.table.resize(3);
		s.config.table[1].name = "Changed";
		s.config.table[1].color = glm::vec3(0.25f, 0.5f, 0.75f);
		return s;
	}

	//Non-square test map: value by the side of a line along x (vertical = false) or y
	HeightField Split(int at, bool vertical, float before, float after)
	{
		HeightField map(40, 24);
		for (int y = 0; y < map.GetHeight(); y++) {
			for (int x = 0; x < map.GetWidth(); x++) {
				map.At(x, y) = (vertical ? y : x) < at ? before : after;
			}
		}
		return map;
	}

	glm::vec2 ClimateAt(const mapbiomes::ClimateMaps& maps, int x, int y)
	{
		return glm::unpackHalf2x16(maps.packed[static_cast<size_t>(y) * maps.width + x]);
	}
}

TEST_CASE("Map biome settings survive JSON")
{
	const mapbiomes::Settings original = DistinctSettings();
	mapbiomes::Settings loaded;
	std::string error;
	REQUIRE_MESSAGE(mapbiomes::FromJson(mapbiomes::ToJson(original), loaded, error), error);
	CHECK(loaded.enabled == original.enabled);
	CHECK(loaded.heightUnitMetres == original.heightUnitMetres);
	CHECK(loaded.baseMetres == original.baseMetres);
	CHECK(loaded.coastMetres == original.coastMetres);
	CHECK(loaded.reliefRadiusMetres == original.reliefRadiusMetres);
	CHECK(loaded.reliefMetres == original.reliefMetres);
	CHECK(loaded.climateOrigin == original.climateOrigin);
	CHECK(loaded.config.blendWidth == original.config.blendWidth);
	CHECK(loaded.config.temperature.seed == original.config.temperature.seed);
	REQUIRE(loaded.config.table.size() == 3);
	CHECK(loaded.config.table[1].name == "Changed");
	CHECK(loaded.config.table[1].color == original.config.table[1].color);
}

TEST_CASE("Bad map biome JSON is rejected")
{
	const mapbiomes::Settings original = DistinctSettings();
	std::string error;
	for (const char* text : { "not json", R"({"heightUnitMetres": 0})", R"({"coastMetres": -1})", R"({"reliefMetres": 0})",
		R"({"reliefRadiusMetres": -5})", R"({"config": {"table": []}})" }) {
		CAPTURE(text);
		mapbiomes::Settings loaded = original;
		error.clear();
		CHECK_FALSE(mapbiomes::FromJson(text, loaded, error));
		CHECK_FALSE(error.empty());
		CHECK(loaded.heightUnitMetres == original.heightUnitMetres);
		CHECK(loaded.config.table.size() == original.config.table.size());
	}
}

TEST_CASE("Default base puts the sea at the TerrainGen water height")
{
	const mapbiomes::Settings withSea = mapbiomes::Default(100.0f);
	CHECK(withSea.heightUnitMetres == 256.0f);
	CHECK(mapbiomes::WaterHeight(withSea, 100.0f) == doctest::Approx(TerrainConfig{}.WaterHeight()));
	const mapbiomes::Settings dry = mapbiomes::Default(std::nullopt);
	CHECK(dry.baseMetres == 0.0f);
	CHECK(mapbiomes::WaterHeight(dry, std::nullopt) == 0.0f);
	CHECK_FALSE(dry.enabled);
}

TEST_CASE("Settings from a TerrainGen preset keep its biomes")
{
	//A preset needs a real map size, as the app's world always has
	WorldConfig world;
	world.width = 512;
	world.height = 384;
	world.biomes.table.resize(2);
	world.biomes.table[0].name = "First";
	world.biomes.temperature.seed = 4242;
	WorldConfig read;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(preset::ToJson(world), read, error), error);
	const mapbiomes::Settings s = mapbiomes::FromGeneration(preset::ToJson(world), glm::ivec2(512, -256), 256.0f, 60.0f, true);
	CHECK(s.enabled);
	REQUIRE(s.config.table.size() == 2);
	CHECK(s.config.table[0].name == "First");
	CHECK(s.config.temperature.seed == 4242);
	CHECK(s.climateOrigin == glm::vec2(512.0f, -256.0f));
	CHECK(s.heightUnitMetres == 256.0f);
	CHECK(s.baseMetres == 0.0f);
}

TEST_CASE("Settings from a noise preset are defaults")
{
	NoiseMapConfig noiseMap;
	noiseMap.width = 256;
	noiseMap.height = 256;
	const mapbiomes::Settings s = mapbiomes::FromGeneration(preset::ToJson(noiseMap), glm::ivec2(64, 32), 64.0f, std::nullopt, true);
	CHECK_FALSE(s.enabled);
	CHECK(s.climateOrigin == glm::vec2(64.0f, 32.0f));
	CHECK(s.config.table.size() == biome::DefaultBiomes().size());
	const mapbiomes::Settings broken = mapbiomes::FromGeneration("{", glm::ivec2(0), 1.0f, std::nullopt, true);
	CHECK_FALSE(broken.enabled);
}

TEST_CASE("Coast distance is signed and in metres")
{
	const HeightField map = Split(20, false, -10.0f, 10.0f);
	const HeightField distance = mapbiomes::CoastDistance(map, glm::dvec2(2.0, 3.0), 0.0f);
	for (int y : { 0, 11, 23 }) {
		CAPTURE(y);
		CHECK(distance.At(25, y) == doctest::Approx(12.0f));
		CHECK(distance.At(17, y) == doctest::Approx(-6.0f));
	}
}

TEST_CASE("Coast distance follows the pixel size of each axis")
{
	const HeightField map = Split(12, true, -10.0f, 10.0f);
	const HeightField distance = mapbiomes::CoastDistance(map, glm::dvec2(2.0, 3.0), 0.0f);
	//Nearest water at y = 11: 4 texels of 3 m
	CHECK(distance.At(5, 15) == doctest::Approx(12.0f));
	CHECK(distance.At(5, 9) == doctest::Approx(-9.0f));
}

TEST_CASE("Local relief sees a spike within the radius only")
{
	HeightField map(40, 24);
	map.At(10, 10) = 100.0f;
	const HeightField relief = mapbiomes::LocalRelief(map, glm::dvec2(2.0, 1.0), 4.0f);
	CHECK(relief.At(12, 10) == 100.0f);
	CHECK(relief.At(13, 10) == 0.0f);
	CHECK(relief.At(10, 14) == 100.0f);
	CHECK(relief.At(10, 15) == 0.0f);
	CHECK(relief.At(8, 6) == 100.0f);
}

TEST_CASE("No sea gives continentalness 1")
{
	const HeightField map = Split(20, false, -10.0f, 10.0f);
	const mapbiomes::ClimateMaps maps = mapbiomes::ComputeClimate(map, glm::dvec2(1.0), std::nullopt, mapbiomes::Settings{});
	REQUIRE(maps.width == 40);
	REQUIRE(maps.height == 24);
	REQUIRE(maps.packed.size() == map.Size());
	for (int x : { 0, 19, 20, 39 }) {
		CHECK(ClimateAt(maps, x, 5).x == 1.0f);
	}
}

TEST_CASE("Climate maps clamp and scale")
{
	mapbiomes::Settings s;
	s.coastMetres = 10.0f;
	s.reliefMetres = 100.0f;
	s.reliefRadiusMetres = 1.0f;
	//Water for x < 20; land with a 50 m step at x = 30
	HeightField map = Split(20, false, -10.0f, 10.0f);
	for (int y = 0; y < map.GetHeight(); y++) {
		for (int x = 30; x < map.GetWidth(); x++) {
			map.At(x, y) = 60.0f;
		}
	}
	const mapbiomes::ClimateMaps maps = mapbiomes::ComputeClimate(map, glm::dvec2(1.0), 0.0f, s);
	CHECK(ClimateAt(maps, 39, 5).x == doctest::Approx(1.0f).epsilon(2e-3));
	CHECK(ClimateAt(maps, 16, 5).x == doctest::Approx(-0.4f).epsilon(2e-3));
	//Relief 50 next to the step, 0 far from it (radius 1 texel)
	CHECK(ClimateAt(maps, 30, 5).y == doctest::Approx(0.0f).epsilon(2e-3));
	CHECK(ClimateAt(maps, 25, 5).y == -1.0f);
}

TEST_CASE("Climate height maps metres to terrain units")
{
	mapbiomes::Settings s;
	s.heightUnitMetres = 100.0f;
	s.baseMetres = -30.0f;
	CHECK(mapbiomes::WaterHeight(s, 0.0f) == doctest::Approx(0.3f));
	CHECK(mapbiomes::ClimateHeight(50.0f, 1.0f, 0.0f, s) == doctest::Approx(0.8f));
	CHECK(mapbiomes::ClimateHeight(0.0f, -1.0f, 0.0f, s) == doctest::Approx(0.0f));
	CHECK(mapbiomes::ClimateHeight(0.0f, -0.5f, 0.0f, s) == doctest::Approx(0.15f));
	CHECK(mapbiomes::ClimateHeight(-0.5f, -0.5f, 0.0f, s) == doctest::Approx(0.15f));
	CHECK(mapbiomes::ClimateHeight(-20.0f, -1.0f, 0.0f, s) == doctest::Approx(0.1f));
	CHECK(mapbiomes::ClimateHeight(50.0f, 1.0f, std::nullopt, s) == doctest::Approx(0.8f));
}

TEST_CASE("Climate maps timing 8192 x 8192" * doctest::skip())
{
	HeightField map(8192, 8192);
	for (int y = 0; y < map.GetHeight(); y++) {
		for (int x = 0; x < map.GetWidth(); x++) {
			map.At(x, y) = 500.0f * std::sin(x * 0.003f) * std::cos(y * 0.002f);
		}
	}
	const auto start = std::chrono::steady_clock::now();
	const mapbiomes::ClimateMaps maps = mapbiomes::ComputeClimate(map, glm::dvec2(30.0), 0.0f, mapbiomes::Settings{});
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	MESSAGE("ComputeClimate 8192 x 8192: " << seconds << " s");
	CHECK(maps.packed.size() == map.Size());
}
