#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Places.h"
#include "Preset.h"

static std::vector<places::Place> SamplePlaces()
{
	places::Place valley;
	valley.name = "Valley";
	valley.camera = glm::vec3(-1234.5f, 310.25f, 8000.125f);
	valley.yaw = 37.5f;
	valley.pitch = -22.25f;
	valley.mapOffset = glm::ivec2(-309, 2000);

	places::Place coast;
	coast.name = "Coast with a long name";
	coast.camera = glm::vec3(1.0f, 2.0f, 3.0f);
	coast.yaw = -90.0f;
	coast.pitch = 89.0f;
	coast.mapOffset = glm::ivec2(preset::maxMapOffset, -preset::maxMapOffset);
	return { valley, coast };
}

static void CheckSamePlaces(const std::vector<places::Place>& a, const std::vector<places::Place>& b)
{
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); i++) {
		CAPTURE(i);
		CHECK(a[i].name == b[i].name);
		CHECK(a[i].camera == b[i].camera);
		CHECK(a[i].yaw == b[i].yaw);
		CHECK(a[i].pitch == b[i].pitch);
		CHECK(a[i].mapOffset == b[i].mapOffset);
	}
}

static std::string Replaced(std::string text, const std::string& from, const std::string& to)
{
	size_t position = text.find(from);
	REQUIRE(position != std::string::npos);
	return text.replace(position, from.size(), to);
}

TEST_CASE("Places keep every field and their order")
{
	std::vector<places::Place> loaded;
	std::string error;
	REQUIRE_MESSAGE(places::FromJson(places::ToJson(SamplePlaces()), loaded, error), error);
	CheckSamePlaces(loaded, SamplePlaces());

	REQUIRE_MESSAGE(places::FromJson(places::ToJson({}), loaded, error), error);
	CHECK(loaded.empty());
}

TEST_CASE("Invalid places are rejected and the list stays unchanged")
{
	const std::string valid = places::ToJson({ SamplePlaces()[0] });
	struct Case {
		const char* name;
		std::string text;
	};
	const Case cases[] = {
		{ "not JSON", "{ not json" },
		{ "not an object", "[1]" },
		{ "a preset", R"({"kind": "world", "version": 2, "places": []})" },
		{ "other version", Replaced(valid, "\"version\": 1", "\"version\": 2") },
		{ "no list", R"({"kind": "places", "version": 1})" },
		{ "empty name", Replaced(valid, "\"Valley\"", "\"\"") },
		{ "name too long", Replaced(valid, "\"Valley\"", "\"" + std::string(places::maxNameLength + 1, 'a') + "\"") },
		{ "no camera", Replaced(valid, "\"camera\"", "\"lens\"") },
		{ "pitch over 90", Replaced(valid, "\"pitch\": -22.25", "\"pitch\": -91") },
		{ "map offset out of range", Replaced(valid, "-309", "-16777217") },
		{ "map offset not integers", Replaced(valid, "-309", "-309.5") },
	};
	for (const Case& c : cases) {
		CAPTURE(c.name);
		std::vector<places::Place> loaded = SamplePlaces();
		std::string error;
		CHECK_FALSE(places::FromJson(c.text, loaded, error));
		CHECK_FALSE(error.empty());
		CheckSamePlaces(loaded, SamplePlaces());
	}
}

TEST_CASE("Places are saved to and loaded from a file")
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_places_tests";
	std::filesystem::remove_all(folder);
	const std::filesystem::path path = folder / "nested" / "world.json";
	std::string error;

	SUBCASE("A missing file is an empty list")
	{
		std::vector<places::Place> loaded = SamplePlaces();
		REQUIRE_MESSAGE(places::Load(path, loaded, error), error);
		CHECK(loaded.empty());
	}
	SUBCASE("Saved places load back")
	{
		REQUIRE_MESSAGE(places::Save(path, SamplePlaces(), error), error);
		std::vector<places::Place> loaded;
		REQUIRE_MESSAGE(places::Load(path, loaded, error), error);
		CheckSamePlaces(loaded, SamplePlaces());
	}
	SUBCASE("An invalid place is not saved and the file stays")
	{
		REQUIRE_MESSAGE(places::Save(path, SamplePlaces(), error), error);
		std::vector<places::Place> invalid = SamplePlaces();
		invalid[1].name.clear();
		CHECK_FALSE(places::Save(path, invalid, error));
		std::vector<places::Place> loaded;
		REQUIRE_MESSAGE(places::Load(path, loaded, error), error);
		CheckSamePlaces(loaded, SamplePlaces());
	}
	SUBCASE("A broken file fails to load")
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path) << "{ broken";
		std::vector<places::Place> loaded;
		CHECK_FALSE(places::Load(path, loaded, error));
		CHECK_FALSE(error.empty());
	}

	std::filesystem::remove_all(folder);
}
