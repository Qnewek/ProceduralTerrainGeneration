#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
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

//--- Land cover settings (stage D3) ---

namespace {
	bool SameClasses(const std::vector<mapbiomes::LandCoverClass>& a, const std::vector<mapbiomes::LandCoverClass>& b)
	{
		if (a.size() != b.size()) {
			return false;
		}
		for (size_t i = 0; i < a.size(); i++) {
			if (a[i].code != b[i].code || a[i].name != b[i].name || a[i].color != b[i].color || a[i].biomes != b[i].biomes) {
				return false;
			}
		}
		return true;
	}

	int IndexOf(const std::vector<biome::Biome>& table, const std::string& name)
	{
		for (size_t i = 0; i < table.size(); i++) {
			if (table[i].name == name) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
}

TEST_CASE("Class masks follow biome names")
{
	mapbiomes::Settings s;
	s.source = mapbiomes::Source::LAND_COVER;
	s.classes = {
		{ 10, "Trees", glm::vec3(0.0f, 0.4f, 0.0f), { "Forest", "Removed", "Taiga" } },
		{ 20, "Gone", glm::vec3(0.5f), { "Removed" } },
		{ 30, "Any", glm::vec3(0.5f), {} },
	};
	const uint64_t forests = (uint64_t(1) << IndexOf(s.config.table, "Forest")) | (uint64_t(1) << IndexOf(s.config.table, "Taiga"));
	std::array<uint64_t, 256> masks = mapbiomes::ClassMasks(s);
	CHECK(masks[10] == forests);
	CHECK(masks[20] == 0);
	CHECK(masks[30] == 0);
	CHECK(masks[40] == 0);
	CHECK(masks[0] == 0);

	//Another order of the table moves the bits with the biomes
	std::rotate(s.config.table.begin(), s.config.table.begin() + 1, s.config.table.end());
	masks = mapbiomes::ClassMasks(s);
	CHECK(masks[10] == ((uint64_t(1) << IndexOf(s.config.table, "Forest")) | (uint64_t(1) << IndexOf(s.config.table, "Taiga"))));
	CHECK(masks[10] != forests);

	//Biomes from the climate: no masks
	s.source = mapbiomes::Source::CLIMATE;
	masks = mapbiomes::ClassMasks(s);
	CHECK(std::all_of(masks.begin(), masks.end(), [](uint64_t m) { return m == 0; }));
}

TEST_CASE("WorldCover classes name existing biomes")
{
	const std::vector<mapbiomes::LandCoverClass> classes = mapbiomes::WorldCoverClasses();
	CHECK(classes.size() == 11);
	const std::vector<biome::Biome> table = BiomeConfig::Default().table;
	for (const mapbiomes::LandCoverClass& c : classes) {
		CHECK_FALSE(c.biomes.empty());
		for (const std::string& name : c.biomes) {
			CHECK_MESSAGE(IndexOf(table, name) >= 0, name);
		}
	}
	CHECK(classes.front().code == 10);
	CHECK(classes.front().name == "Tree cover");
	CHECK(classes.back().code == 100);
	mapbiomes::Settings s;
	s.source = mapbiomes::Source::LAND_COVER;
	const std::array<uint64_t, 256> masks = mapbiomes::ClassMasks(s);
	CHECK(masks[80] == ((uint64_t(1) << IndexOf(table, "Ocean")) | (uint64_t(1) << IndexOf(table, "Deep ocean"))));
}

TEST_CASE("Land cover settings survive JSON")
{
	mapbiomes::Settings s;
	s.source = mapbiomes::Source::LAND_COVER;
	s.landCoverFiles = { "C:/maps/ESA_WorldCover_N51E018.tif", "zakopane.tif" };
	s.classes = mapbiomes::WorldCoverClasses();
	s.classes[0].name = "Lasy";
	s.classes[0].color = glm::vec3(0.25f, 0.5f, 0.125f);
	s.classes[0].biomes = { "Jungle" };
	s.classes.push_back({ 200, "Class 200", glm::vec3(0.5f), {} });
	mapbiomes::Settings read;
	std::string error;
	REQUIRE_MESSAGE(mapbiomes::FromJson(mapbiomes::ToJson(s), read, error), error);
	CHECK(read.source == mapbiomes::Source::LAND_COVER);
	CHECK(read.landCoverFiles == s.landCoverFiles);
	CHECK(SameClasses(read.classes, s.classes));
}

TEST_CASE("Old biome JSON keeps the climate source")
{
	mapbiomes::Settings read;
	std::string error;
	REQUIRE_MESSAGE(mapbiomes::FromJson("{\"enabled\":true}", read, error), error);
	CHECK(read.source == mapbiomes::Source::CLIMATE);
	CHECK(read.landCoverFiles.empty());
	CHECK(SameClasses(read.classes, mapbiomes::WorldCoverClasses()));
}

TEST_CASE("Repeated class code fails")
{
	mapbiomes::Settings read;
	std::string error;
	const std::string twice = "{\"classes\":[{\"code\":10,\"name\":\"A\",\"color\":[0,0,0],\"biomes\":[]},"
		"{\"code\":10,\"name\":\"B\",\"color\":[0,0,0],\"biomes\":[]}]}";
	CHECK_FALSE(mapbiomes::FromJson(twice, read, error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(mapbiomes::FromJson("{\"classes\":[{\"code\":0,\"name\":\"A\",\"color\":[0,0,0],\"biomes\":[]}]}", read, error));
	CHECK_FALSE(mapbiomes::FromJson("{\"classes\":[{\"code\":256,\"name\":\"A\",\"color\":[0,0,0],\"biomes\":[]}]}", read, error));
	CHECK_FALSE(mapbiomes::FromJson("{\"source\":\"satellite\"}", read, error));
	CHECK(SameClasses(read.classes, mapbiomes::WorldCoverClasses()));
}

TEST_CASE("Missing classes are added")
{
	mapbiomes::Settings s;
	std::array<uint64_t, 256> counts{};
	counts[0] = 100;
	counts[10] = 3;
	counts[42] = 5;
	counts[250] = 1;
	const size_t before = s.classes.size();
	mapbiomes::AddMissingClasses(s, counts);
	REQUIRE(s.classes.size() == before + 2);
	CHECK(std::is_sorted(s.classes.begin(), s.classes.end(), [](const auto& a, const auto& b) { return a.code < b.code; }));
	const auto added = std::find_if(s.classes.begin(), s.classes.end(), [](const auto& c) { return c.code == 42; });
	REQUIRE(added != s.classes.end());
	CHECK(added->name == "Class 42");
	CHECK(added->biomes.empty());
	mapbiomes::AddMissingClasses(s, counts);
	CHECK(s.classes.size() == before + 2);
}

TEST_CASE("Land cover files follow the project folder")
{
	auto utf8 = [](const std::filesystem::path& p) { const std::u8string t = p.u8string(); return std::string(t.begin(), t.end()); };
	const std::filesystem::path root = std::filesystem::temp_directory_path() / "ptg_landcover_project";
	std::filesystem::remove_all(root);
	const std::filesystem::path before = root / "before", after = root / "after", elsewhere = root / "elsewhere.tif";
	std::filesystem::create_directories(before / "cover");
	std::filesystem::create_directories(after / "cover");
	std::ofstream(before / "cover" / "tile.tif") << "x";
	std::ofstream(after / "cover" / "tile.tif") << "x";
	std::ofstream(elsewhere) << "x";

	mapbiomes::Settings s;
	s.landCoverFiles = { utf8(before / "cover" / "tile.tif"), utf8(elsewhere) };
	const std::string json = mapbiomes::ToJson(s, before);
	//The project and its files moved together: the old path is gone, the one beside the project is taken
	std::filesystem::remove_all(before);
	mapbiomes::Settings read;
	std::string error;
	REQUIRE_MESSAGE(mapbiomes::FromJson(json, read, error, after), error);
	REQUIRE(read.landCoverFiles.size() == 2);
	CHECK(std::filesystem::path(std::u8string(read.landCoverFiles[0].begin(), read.landCoverFiles[0].end())) == (after / "cover" / "tile.tif").lexically_normal());
	//A path that still exists stays as it was
	CHECK(read.landCoverFiles[1] == utf8(elsewhere));
	//Without a folder the paths are read as saved
	REQUIRE_MESSAGE(mapbiomes::FromJson(json, read, error), error);
	CHECK(read.landCoverFiles[0] == utf8(before / "cover" / "tile.tif"));
	std::filesystem::remove_all(root);
}
