#include "doctest/doctest.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "MapProject.h"

//Non-square map with fractional values, so swapped axes and lost precision are detected
static HeightField SmallMap(float add = 0.0f)
{
	HeightField map(5, 3);
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 5; x++) {
			map.At(x, y) = static_cast<float>(y * 10 + x) - 7.25f + add;
		}
	}
	return map;
}

static bool SameBits(const HeightField& a, const HeightField& b)
{
	return a.GetWidth() == b.GetWidth() && a.GetHeight() == b.GetHeight() &&
		(a.Empty() || std::memcmp(a.Data(), b.Data(), a.Size() * sizeof(float)) == 0);
}

static mapproject::Project FullProject()
{
	mapproject::Project project;
	project.working = SmallMap();
	project.original = SmallMap(1.0f);
	project.pixelMetres = glm::dvec2(30.0, 28.0);
	project.seaLevel = 12.5f;
	project.source = "alps.tif";
	project.generation = "{\"a\":1}";
	project.verticalExaggeration = 2.0f;
	return project;
}

//A project the failed reads must leave untouched
static mapproject::Project Marker()
{
	mapproject::Project project;
	project.working = HeightField(2, 2);
	for (size_t i = 0; i < project.working.Size(); i++) {
		project.working[i] = 9.0f;
	}
	project.source = "marker";
	return project;
}

static bool IsMarker(const mapproject::Project& project)
{
	return project.source == "marker" && project.working.GetWidth() == 2 && project.working[0] == 9.0f;
}

static std::vector<uint8_t> Bytes(const mapproject::Project& project)
{
	std::vector<uint8_t> bytes;
	std::string error;
	REQUIRE(mapproject::Serialize(project, bytes, error));
	return bytes;
}

TEST_CASE("Project round trip keeps every field")
{
	const mapproject::Project project = FullProject();
	const std::vector<uint8_t> bytes = Bytes(project);
	mapproject::Project read;
	std::string error;
	REQUIRE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK(SameBits(read.working, project.working));
	CHECK(SameBits(read.original, project.original));
	CHECK(read.pixelMetres == project.pixelMetres);
	REQUIRE(read.seaLevel.has_value());
	CHECK(*read.seaLevel == 12.5f);
	CHECK(read.source == "alps.tif");
	CHECK(read.generation == "{\"a\":1}");
	CHECK(read.verticalExaggeration == 2.0f);
}

TEST_CASE("Project without original, sea level and generation")
{
	mapproject::Project project;
	project.working = SmallMap();
	const std::vector<uint8_t> bytes = Bytes(project);
	mapproject::Project read = FullProject();
	std::string error;
	REQUIRE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK(SameBits(read.working, project.working));
	CHECK(read.original.Empty());
	CHECK_FALSE(read.seaLevel.has_value());
	CHECK(read.generation.empty());
	CHECK(read.source.empty());
}

TEST_CASE("Truncated project fails")
{
	const std::vector<uint8_t> bytes = Bytes(FullProject());
	for (size_t size : { bytes.size() - 1, bytes.size() / 2, size_t(10), size_t(0) }) {
		CAPTURE(size);
		mapproject::Project read = Marker();
		std::string error;
		CHECK_FALSE(mapproject::Deserialize(bytes.data(), size, read, error));
		CHECK_FALSE(error.empty());
		CHECK(IsMarker(read));
	}
}

TEST_CASE("Damaged project fails")
{
	const std::vector<uint8_t> good = Bytes(FullProject());
	SUBCASE("Damaged compressed data")
	{
		std::vector<uint8_t> bytes = good;
		bytes[bytes.size() - 8] ^= 0xFF;
		mapproject::Project read = Marker();
		std::string error;
		CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
		CHECK_FALSE(error.empty());
		CHECK(IsMarker(read));
	}
	SUBCASE("Wrong signature")
	{
		std::vector<uint8_t> bytes = good;
		bytes[0] = 'X';
		mapproject::Project read = Marker();
		std::string error;
		CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
		CHECK(IsMarker(read));
	}
}

TEST_CASE("Project from a newer version fails")
{
	std::vector<uint8_t> bytes = Bytes(FullProject());
	const std::string from = "\"version\":1";
	std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	bytes[at + from.size() - 1] = '2';
	mapproject::Project read = Marker();
	std::string error;
	CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK(error.find("newer") != std::string::npos);
	CHECK(IsMarker(read));
}

TEST_CASE("Non-finite heights are not saved")
{
	std::vector<uint8_t> bytes;
	std::string error;
	mapproject::Project project = FullProject();
	project.working[3] = std::numeric_limits<float>::quiet_NaN();
	CHECK_FALSE(mapproject::Serialize(project, bytes, error));
	CHECK_FALSE(error.empty());

	project = FullProject();
	project.original[2] = std::numeric_limits<float>::infinity();
	CHECK_FALSE(mapproject::Serialize(project, bytes, error));

	project = FullProject();
	project.original = HeightField(4, 3);
	CHECK_FALSE(mapproject::Serialize(project, bytes, error));

	project = FullProject();
	project.working = HeightField();
	CHECK_FALSE(mapproject::Serialize(project, bytes, error));
}

TEST_CASE("Save and Load a file")
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_project_tests";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);

	const mapproject::Project project = FullProject();
	const std::filesystem::path path = folder / "alps.ptgmap";
	std::string error;
	REQUIRE(mapproject::Save(path, project, error));
	mapproject::Project read;
	REQUIRE(mapproject::Load(path, read, error));
	CHECK(SameBits(read.working, project.working));
	CHECK(SameBits(read.original, project.original));
	CHECK(read.source == project.source);

	std::ofstream(folder / "map.png") << "not a project";
	const std::vector<std::filesystem::path> listed = mapproject::ListProjects(folder);
	REQUIRE(listed.size() == 1);
	CHECK(listed[0].filename() == "alps.ptgmap");
	CHECK(mapproject::ListProjects(folder / "missing").empty());

	//A file where a folder should be
	CHECK_FALSE(mapproject::Save(folder / "map.png" / "x.ptgmap", project, error));
	CHECK_FALSE(error.empty());

	mapproject::Project marker = Marker();
	CHECK_FALSE(mapproject::Load(folder / "missing.ptgmap", marker, error));
	CHECK(IsMarker(marker));
}

TEST_CASE("Project paths get the project extension")
{
	CHECK(mapproject::WithExtension("dem.v2") == std::filesystem::path("dem.v2.ptgmap"));
	CHECK(mapproject::WithExtension("folder/alps") == std::filesystem::path("folder/alps.ptgmap"));
	CHECK(mapproject::WithExtension("alps.ptgmap") == std::filesystem::path("alps.ptgmap"));
	CHECK(mapproject::WithExtension("ALPS.PTGMAP") == std::filesystem::path("ALPS.PTGMAP"));
}

TEST_CASE("Smooth map compresses")
{
	mapproject::Project project;
	project.working = HeightField(256, 256);
	for (int y = 0; y < 256; y++) {
		for (int x = 0; x < 256; x++) {
			project.working.At(x, y) = static_cast<float>(300.0 * std::sin(x / 40.0) * std::cos(y / 50.0));
		}
	}
	CHECK(Bytes(project).size() < size_t(256) * 256 * 4);
}
