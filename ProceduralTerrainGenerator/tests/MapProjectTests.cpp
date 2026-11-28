#include "doctest/doctest.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
	project.rivers = "{\"cellSize\":4}";
	project.biomes = "{\"enabled\":true}";
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
	CHECK(read.rivers == "{\"cellSize\":4}");
}

//A project with biomes painted over two texels
static mapproject::Project PaintedProject()
{
	mapproject::Project project = FullProject();
	project.paint = biomepaint::Make(project.working.GetWidth(), project.working.GetHeight());
	project.paint.palette = { "A", "B" };
	project.paint.texels[3] = static_cast<uint16_t>((255 << 8) | 1);
	project.paint.texels[11] = static_cast<uint16_t>((40 << 8) | 2);
	return project;
}

TEST_CASE("Project keeps its biome paint")
{
	CHECK(mapproject::currentVersion == 7);
	const mapproject::Project project = PaintedProject();
	const std::vector<uint8_t> bytes = Bytes(project);
	mapproject::Project read;
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	CHECK(read.paint.width == project.paint.width);
	CHECK(read.paint.height == project.paint.height);
	CHECK(read.paint.palette == project.paint.palette);
	CHECK(read.paint.texels == project.paint.texels);
	CHECK(SameBits(read.working, project.working));
}

TEST_CASE("Version 3 project has no biome paint")
{
	mapproject::Project project;
	project.working = SmallMap();
	project.biomes = "{\"enabled\":true}";
	std::vector<uint8_t> bytes = Bytes(project);
	const std::string from = "\"version\":" + std::to_string(mapproject::currentVersion);
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	bytes[at + from.size() - 1] = '3';
	mapproject::Project read = PaintedProject();
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	CHECK(read.paint.Empty());
	CHECK(read.biomes == "{\"enabled\":true}");
}

TEST_CASE("Paint of another size is not saved")
{
	mapproject::Project project = PaintedProject();
	project.paint = biomepaint::Make(3, 3);
	project.paint.palette = { "A" };
	project.paint.texels[0] = static_cast<uint16_t>((255 << 8) | 1);
	std::vector<uint8_t> bytes;
	std::string error;
	CHECK_FALSE(mapproject::Serialize(project, bytes, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("Paint with a slot past its palette fails")
{
	std::vector<uint8_t> bytes = Bytes(PaintedProject());
	//The same length, so the header length stays right: slot 2 has no name now
	const std::string from = "[\"A\",\"B\"]", to = "[\"A\"]    ";
	REQUIRE(from.size() == to.size());
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	std::copy(to.begin(), to.end(), bytes.begin() + at);
	mapproject::Project read = Marker();
	std::string error;
	CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK_FALSE(error.empty());
	CHECK(IsMarker(read));
}

TEST_CASE("Project keeps its biome settings")
{
	const mapproject::Project project = FullProject();
	const std::vector<uint8_t> bytes = Bytes(project);
	mapproject::Project read;
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	CHECK(read.biomes == "{\"enabled\":true}");
}

TEST_CASE("Version 2 project has no biome settings")
{
	mapproject::Project project;
	project.working = SmallMap();
	project.rivers = "{\"cellSize\":4}";
	std::vector<uint8_t> bytes = Bytes(project);
	const std::string from = "\"version\":" + std::to_string(mapproject::currentVersion);
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	bytes[at + from.size() - 1] = '2';
	mapproject::Project read = FullProject();
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	CHECK(read.biomes.empty());
	CHECK(read.rivers == "{\"cellSize\":4}");
}

TEST_CASE("Project of version 1 is read without rivers")
{
	mapproject::Project project;
	project.working = SmallMap();
	std::vector<uint8_t> bytes = Bytes(project);
	const std::string from = "\"version\":" + std::to_string(mapproject::currentVersion);
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	REQUIRE(mapproject::currentVersion < 10);
	bytes[at + from.size() - 1] = '1';
	mapproject::Project read = FullProject();
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	CHECK(SameBits(read.working, project.working));
	CHECK(read.rivers.empty());
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
	const std::string from = "\"version\":" + std::to_string(mapproject::currentVersion);
	std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	REQUIRE(mapproject::currentVersion < 9);
	bytes[at + from.size() - 1] = static_cast<uint8_t>('0' + mapproject::currentVersion + 1);
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

static mapproject::Project GeoProject()
{
	mapproject::Project project = FullProject();
	project.georeference = geo::GeoReference{ 2180, { 1234.5, 300000.125 }, { 30.0, -29.5 } };
	return project;
}

TEST_CASE("Project keeps its georeference")
{
	const mapproject::Project project = GeoProject();
	const std::vector<uint8_t> bytes = Bytes(project);
	mapproject::Project read;
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	REQUIRE(read.georeference.has_value());
	CHECK(read.georeference->epsg == 2180);
	CHECK(read.georeference->origin == project.georeference->origin);
	CHECK(read.georeference->pixel == project.georeference->pixel);

	//None stays none
	const std::vector<uint8_t> plain = Bytes(FullProject());
	read = GeoProject();
	REQUIRE_MESSAGE(mapproject::Deserialize(plain.data(), plain.size(), read, error), error);
	CHECK_FALSE(read.georeference.has_value());
}

TEST_CASE("Version 4 project has no georeference")
{
	std::vector<uint8_t> bytes = Bytes(GeoProject());
	const std::string from = "\"version\":" + std::to_string(mapproject::currentVersion);
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	bytes[at + from.size() - 1] = '4';
	mapproject::Project read = GeoProject();
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	CHECK_FALSE(read.georeference.has_value());
}

TEST_CASE("Project keeps its vegetation settings")
{
	mapproject::Project project = FullProject();
	project.vegetation.enabled = false;
	project.vegetation.density = 2.5f;
	const std::vector<uint8_t> bytes = Bytes(project);
	mapproject::Project read;
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	CHECK_FALSE(read.vegetation.enabled);
	CHECK(read.vegetation.density == 2.5f);
}

TEST_CASE("Version 5 project has default vegetation")
{
	mapproject::Project project = FullProject();
	project.vegetation.enabled = false;
	project.vegetation.density = 2.5f;
	std::vector<uint8_t> bytes = Bytes(project);
	const std::string from = "\"version\":" + std::to_string(mapproject::currentVersion);
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	bytes[at + from.size() - 1] = '5';
	mapproject::Project read;
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	CHECK(read.vegetation.enabled);
	CHECK(read.vegetation.density == 1.0f);
}

TEST_CASE("Damaged vegetation settings fail")
{
	mapproject::Project project = FullProject();
	project.vegetation.density = 2.5f;
	std::vector<uint8_t> bytes = Bytes(project);
	//The same length, so the header length stays right: a density above 3
	const std::string from = "\"density\":2.5", to = "\"density\":9.5";
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	std::copy(to.begin(), to.end(), bytes.begin() + at);
	mapproject::Project read = Marker();
	std::string error;
	CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK_FALSE(error.empty());
	CHECK(IsMarker(read));
}

namespace {
	placedplants::Layer TwoPlants()
	{
		placedplants::Layer layer;
		layer.palette = { "oak", "pine" };
		layer.plants.resize(2);
		layer.plants[0].position = glm::vec2(1.5f, 2.5f);
		layer.plants[0].model = 1;
		layer.plants[0].variant = 2;
		layer.plants[1].position = glm::vec2(30.0f, 4.0f);
		layer.plants[1].yaw = 0.5f;
		layer.plants[1].scale = 1.1f;
		return layer;
	}
}

TEST_CASE("Project keeps its placed plants")
{
	mapproject::Project project = FullProject();
	project.placedPlants = TwoPlants();
	const std::vector<uint8_t> bytes = Bytes(project);
	mapproject::Project read;
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	CHECK(read.placedPlants == project.placedPlants);
}

TEST_CASE("Version 6 project has no placed plants")
{
	mapproject::Project project = FullProject();
	std::vector<uint8_t> bytes = Bytes(project);
	const std::string from = "\"version\":" + std::to_string(mapproject::currentVersion);
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	bytes[at + from.size() - 1] = '6';
	mapproject::Project read;
	std::string error;
	REQUIRE_MESSAGE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error), error);
	CHECK(read.placedPlants.plants.empty());
	CHECK(read.placedPlants.palette.empty());
}

TEST_CASE("Damaged placed plants fail")
{
	mapproject::Project project = FullProject();
	project.placedPlants = TwoPlants();
	//The palette cut to one name (spaces keep the header length): the plant of model 1 names a model the project does not list
	std::vector<uint8_t> bytes = Bytes(project);
	const std::string from = "[\"oak\",\"pine\"]", to = "[\"oak\"]       ";
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	std::copy(to.begin(), to.end(), bytes.begin() + at);
	mapproject::Project read = Marker();
	std::string error;
	CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK_FALSE(error.empty());
	CHECK(IsMarker(read));
}

TEST_CASE("Placed plant count far above the block fails at once")
{
	mapproject::Project project = FullProject();
	project.placedPlants = TwoPlants();
	//99999999 plants (1.9 GB unpacked) said by the header of a block of a few bytes; the palette shortened to keep the header length
	std::vector<uint8_t> bytes = Bytes(project);
	const std::string from = "\"count\":2,\"palette\":[\"oak\",\"pine\"]", to = "\"count\":99999999,\"palette\":[\"\",\"\"]";
	REQUIRE(from.size() == to.size());
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	std::copy(to.begin(), to.end(), bytes.begin() + at);
	mapproject::Project read = Marker();
	std::string error;
	const auto start = std::chrono::steady_clock::now();
	CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < 0.1);
	CHECK_FALSE(error.empty());
	CHECK(IsMarker(read));
}

TEST_CASE("Damaged georeference fails")
{
	std::vector<uint8_t> bytes = Bytes(GeoProject());
	//The same length, so the header length stays right
	const std::string from = "1234.5", to = "1e400 ";
	const std::string text(bytes.begin(), bytes.end());
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	std::copy(to.begin(), to.end(), bytes.begin() + at);
	mapproject::Project read = Marker();
	std::string error;
	CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK_FALSE(error.empty());
	CHECK(IsMarker(read));
}

//The file with its JSON header changed by edit; the blocks stay as they were
template <typename Edit>
static std::vector<uint8_t> WithHeader(const std::vector<uint8_t>& bytes, Edit edit)
{
	const size_t prefix = 12;
	uint32_t length = 0;
	for (size_t i = 0; i < 4; i++) {
		length |= static_cast<uint32_t>(bytes[8 + i]) << (8 * i);
	}
	nlohmann::json header = nlohmann::json::parse(bytes.begin() + prefix, bytes.begin() + prefix + length);
	edit(header);
	const std::string text = header.dump();
	std::vector<uint8_t> out(bytes.begin(), bytes.begin() + 8);
	for (size_t i = 0; i < 4; i++) {
		out.push_back(static_cast<uint8_t>(text.size() >> (8 * i)));
	}
	out.insert(out.end(), text.begin(), text.end());
	out.insert(out.end(), bytes.begin() + prefix + length, bytes.end());
	return out;
}

TEST_CASE("Project: a block too small for its map is damage, found before reading it")
{
	//A few hundred bytes cannot hold 8192 x 8192 floats: no 256 MB per map is allocated for them
	const std::vector<uint8_t> bytes = WithHeader(Bytes(FullProject()), [](nlohmann::json& h) { h["width"] = 8192; h["height"] = 8192; });
	mapproject::Project read = Marker();
	std::string error;
	CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK_MESSAGE(error.find("too small for") != std::string::npos, error);
	CHECK(IsMarker(read));
}

TEST_CASE("Project: a block too small for its painted biomes is damage")
{
	//Big enough that 2 compressed bytes cannot hold its paint (2 bytes per texel)
	mapproject::Project project;
	project.working = HeightField(300, 200);
	project.paint = biomepaint::Make(300, 200);
	project.paint.palette = { "A" };
	project.paint.texels[5] = static_cast<uint16_t>((255 << 8) | 1);
	const std::vector<uint8_t> bytes = WithHeader(Bytes(project), [](nlohmann::json& h) {
		//The paint block claims 2 bytes; the map blocks keep their sizes, so the file still holds all of them
		for (nlohmann::json& block : h["blocks"]) {
			if (block["name"] == "biomePaint") {
				block["bytes"] = 2;
			}
		}
	});
	mapproject::Project read = Marker();
	std::string error;
	CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK_MESSAGE(error.find("too small for") != std::string::npos, error);
	CHECK(IsMarker(read));
}

TEST_CASE("Project: a sea level that is not finite is damage")
{
	const std::vector<uint8_t> bytes = WithHeader(Bytes(FullProject()), [](nlohmann::json& h) { h["seaLevel"] = 1e39; });
	mapproject::Project read = Marker();
	std::string error;
	CHECK_FALSE(mapproject::Deserialize(bytes.data(), bytes.size(), read, error));
	CHECK_MESSAGE(error.find("sea level") != std::string::npos, error);
	CHECK(IsMarker(read));
}

TEST_CASE("Project: oversized placedPlants count and decompression bombs are rejected before allocation")
{
	const std::vector<uint8_t> oversized = WithHeader(Bytes(FullProject()), [](nlohmann::json& h) {
		h["placedPlants"] = { { "palette", { "Fir" } }, { "count", size_t(20'000'000) } };
		h["blocks"].push_back({ { "name", "placedPlants" }, { "bytes", 0 } });
	});
	mapproject::Project read = Marker();
	std::string error;
	CHECK_FALSE(mapproject::Deserialize(oversized.data(), oversized.size(), read, error));
	CHECK(IsMarker(read));

	const std::vector<uint8_t> bomb = WithHeader(Bytes(FullProject()), [](nlohmann::json& h) {
		h["placedPlants"] = { { "palette", { "Fir" } }, { "count", size_t(1'000'000) } };
		h["blocks"].push_back({ { "name", "placedPlants" }, { "bytes", 0 } });
	});
	error.clear();
	CHECK_FALSE(mapproject::Deserialize(bomb.data(), bomb.size(), read, error));
	CHECK(IsMarker(read));
}
