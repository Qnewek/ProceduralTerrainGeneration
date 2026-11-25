#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "ExportManifest.h"

using Json = nlohmann::json;

static exportpack::Manifest SampleManifest()
{
	exportpack::Manifest m;
	m.name = "alps";
	m.mode = "TerrainGen";
	m.width = 1025;
	m.height = 513;
	m.metresPerUnit = 256.0f;
	m.range = heightmap::terrainRange;
	m.mapValues = { 0.125f, 0.75f };
	m.seaLevel = 0.0625f;
	m.files = { exportpack::HeightFile("alps_height.png", "height", heightmap::Format::PNG16),
		exportpack::HeightFile("alps_height.exr", "height", heightmap::Format::EXR32) };
	return m;
}

TEST_CASE("Manifest gives the size and heights in metres")
{
	const Json j = Json::parse(exportpack::ToJson(SampleManifest()));
	CHECK(j["manifestVersion"] == 1);
	CHECK(j["mode"] == "TerrainGen");
	CHECK(j["grid"]["width"] == 1025);
	CHECK(j["grid"]["height"] == 513);
	//Texel centres: 1025 samples span 1024 m
	CHECK(j["grid"]["sizeMetres"][0].get<float>() == doctest::Approx(1024.0f));
	CHECK(j["grid"]["sizeMetres"][1].get<float>() == doctest::Approx(512.0f));
	CHECK(j["height"]["mapMinMetres"].get<float>() == doctest::Approx(32.0f));
	CHECK(j["height"]["mapMaxMetres"].get<float>() == doctest::Approx(192.0f));
	CHECK(j["seaLevel"]["metres"].get<float>() == doctest::Approx(16.0f));
	CHECK(j["grid"]["worldOrigin"] == Json::array({ 0, 0 }));
}

TEST_CASE("Manifest uses the pixel size of the pack")
{
	exportpack::Manifest m = SampleManifest();
	m.width = 11;
	m.height = 6;
	m.metresPerTexel = 30.0f;
	const Json j = Json::parse(exportpack::ToJson(m));
	CHECK(j["grid"]["metresPerTexel"].get<float>() == doctest::Approx(30.0f));
	CHECK(j["grid"]["sizeMetres"][0].get<float>() == doctest::Approx(300.0f));
	CHECK(j["grid"]["sizeMetres"][1].get<float>() == doctest::Approx(150.0f));
	CHECK(j["import"]["unreal"]["scale"][0].get<float>() == doctest::Approx(3000.0f));
	CHECK(j["import"]["unity"]["terrainSize"][0].get<float>() == doctest::Approx(300.0f));
	CHECK(j["import"]["unity"]["terrainSize"][2].get<float>() == doctest::Approx(150.0f));
}

TEST_CASE("Manifest gives where a moved partial map lies in the world")
{
	exportpack::Manifest m = SampleManifest();
	m.worldOrigin = glm::ivec2(-1536, 2048);
	const Json j = Json::parse(exportpack::ToJson(m));
	CHECK(j["grid"]["worldOrigin"][0] == -1536);
	CHECK(j["grid"]["worldOrigin"][1] == 2048);
}

TEST_CASE("Manifest describes the encoding of every file")
{
	const Json j = Json::parse(exportpack::ToJson(SampleManifest()));
	REQUIRE(j["files"].size() == 2);
	const Json& png = j["files"][0];
	CHECK(png["path"] == "alps_height.png");
	CHECK(png["format"] == "png16");
	CHECK(png["encoding"]["type"] == "uint16");
	CHECK(png["encoding"]["metresMin"].get<float>() == doctest::Approx(0.0f));
	CHECK(png["encoding"]["metresMax"].get<float>() == doctest::Approx(256.0f));
	const Json& exr = j["files"][1];
	CHECK(exr["format"] == "exr32");
	CHECK(exr["encoding"]["type"] == "float32");
}

TEST_CASE("Manifest without a sea, generation parameters, materials or biomes leaves them out")
{
	exportpack::Manifest m = SampleManifest();
	m.seaLevel.reset();
	const Json j = Json::parse(exportpack::ToJson(m));
	CHECK_FALSE(j.contains("seaLevel"));
	CHECK_FALSE(j.contains("generation"));
	CHECK_FALSE(j.contains("materials"));
	CHECK_FALSE(j.contains("biomes"));
	CHECK_FALSE(j["import"]["unity"].contains("layers"));
}

TEST_CASE("Manifest lists the materials with their splat channel and weight map, and the biomes")
{
	exportpack::Manifest m = SampleManifest();
	m.files.push_back(exportpack::Png8File("alps_splat_0.png", "splat", 4));
	m.files.push_back(exportpack::Png8File("alps_weight_05_rock.png", "weight", 1));
	exportpack::Material rock;
	rock.id = 5;
	rock.name = "Rock";
	rock.splatFile = "alps_splat_1.png";
	rock.splatChannel = 1;
	rock.weightFile = "alps_weight_05_rock.png";
	rock.coverage = 0.25f;
	exportpack::Material snow;
	snow.id = 6;
	snow.name = "Snow";
	snow.splatFile = "alps_splat_1.png";
	snow.splatChannel = 2;
	m.materials = { rock, snow };
	exportpack::Biome forest;
	forest.id = 3;
	forest.name = "Forest";
	forest.color[1] = 0.5f;
	forest.coverage = 0.4f;
	m.biomes = { forest };

	const Json j = Json::parse(exportpack::ToJson(m));
	CHECK(j["files"][2]["format"] == "png8 rgba");
	CHECK(j["files"][3]["format"] == "png8 grey");
	CHECK_FALSE(j["files"][2].contains("encoding"));
	const Json& materials = j["materials"]["list"];
	REQUIRE(materials.size() == 2);
	CHECK(materials[0]["id"] == 5);
	CHECK(materials[0]["splat"]["file"] == "alps_splat_1.png");
	CHECK(materials[0]["splat"]["channel"] == "G");
	CHECK(materials[0]["weightFile"] == "alps_weight_05_rock.png");
	CHECK(materials[1]["splat"]["channel"] == "B");
	CHECK(materials[1]["weightFile"].is_null());
	const Json& biomes = j["biomes"]["list"];
	REQUIRE(biomes.size() == 1);
	CHECK(biomes[0]["name"] == "Forest");
	CHECK(biomes[0]["color"][1].get<float>() == doctest::Approx(0.5f));
	CHECK(biomes[0]["coverage"].get<float>() == doctest::Approx(0.4f));
	CHECK(j["import"]["unity"].contains("layers"));
}

TEST_CASE("Manifest writes material textures and fallback color")
{
	exportpack::Manifest m = SampleManifest();
	exportpack::Material grass;
	grass.id = 0;
	grass.name = "Grass";
	grass.splatFile = "alps_splat_0.png";
	grass.coverage = 0.6f;
	grass.albedo = "materials/Grass004/albedo.jpg";
	grass.normal = "materials/Grass004/normal.jpg";
	grass.roughness = "materials/Grass004/roughness.jpg";
	grass.tileMetres = 1.0f;
	grass.fallbackColor[0] = 0.3f;
	grass.fallbackColor[1] = 0.4f;
	grass.fallbackColor[2] = 0.15f;
	exportpack::Material snow;
	snow.id = 1;
	snow.name = "Snow";
	snow.splatFile = "alps_splat_0.png";
	snow.splatChannel = 1;
	snow.tileMetres = 1.0f;
	snow.fallbackColor[0] = 0.9f;
	m.materials = { grass, snow };
	const Json j = Json::parse(exportpack::ToJson(m));
	const Json& list = j["materials"]["list"];
	REQUIRE(list.size() == 2);
	CHECK(list[0]["textures"]["albedo"] == "materials/Grass004/albedo.jpg");
	CHECK(list[0]["textures"]["normal"] == "materials/Grass004/normal.jpg");
	CHECK(list[0]["textures"]["roughness"] == "materials/Grass004/roughness.jpg");
	CHECK(list[0]["textures"]["tileMetres"].get<float>() == doctest::Approx(1.0f));
	REQUIRE(list[0]["fallbackColor"].size() == 3);
	CHECK(list[0]["fallbackColor"][2].get<float>() == doctest::Approx(0.15f));
	//No albedo: no textures, the fallback color is still there
	CHECK(list[1]["textures"].is_null());
	CHECK(list[1]["fallbackColor"][0].get<float>() == doctest::Approx(0.9f));
	CHECK(j["materials"].contains("texturesNote"));
}

TEST_CASE("Manifest embeds the generation parameters as JSON, or as text if they are not JSON")
{
	exportpack::Manifest m = SampleManifest();
	m.generation = R"({"version": 2, "seed": 7})";
	CHECK(Json::parse(exportpack::ToJson(m))["generation"]["seed"] == 7);

	m.generation = "not json";
	CHECK(Json::parse(exportpack::ToJson(m))["generation"] == "not json");
}

TEST_CASE("A pack gives back its mode, generation parameters and world position")
{
	exportpack::Manifest m = SampleManifest();
	m.worldOrigin = glm::ivec2(-1536, 2048);
	m.generation = R"({"kind": "world", "version": 2, "width": 64})";
	exportpack::PackSource source;
	std::string error;
	REQUIRE_MESSAGE(exportpack::ReadSource(exportpack::ToJson(m), source, error), error);
	CHECK(source.mode == "TerrainGen");
	CHECK(source.worldOrigin == glm::ivec2(-1536, 2048));
	const Json generation = Json::parse(source.generation);
	CHECK(generation["kind"] == "world");
	CHECK(generation["width"] == 64);

	SUBCASE("Parameters stored as text come back as that text")
	{
		m.generation = "not json";
		REQUIRE_MESSAGE(exportpack::ReadSource(exportpack::ToJson(m), source, error), error);
		CHECK(source.generation == "not json");
	}
	SUBCASE("A pack without parameters or world position (older packs) lies at the world origin")
	{
		Json j = Json::parse(exportpack::ToJson(SampleManifest()));
		j["grid"].erase("worldOrigin");
		REQUIRE_MESSAGE(exportpack::ReadSource(j.dump(), source, error), error);
		CHECK(source.generation.empty());
		CHECK(source.worldOrigin == glm::ivec2(0));
	}
}

TEST_CASE("Text that is not a manifest of this generator is not read as a pack")
{
	exportpack::PackSource source;
	source.mode = "kept";
	std::string error;
	CHECK_FALSE(exportpack::ReadSource("{ not json", source, error));
	CHECK_FALSE(exportpack::ReadSource(R"({"kind": "world", "version": 2})", source, error));
	Json j = Json::parse(exportpack::ToJson(SampleManifest()));
	j["grid"]["worldOrigin"] = Json::array({ 1.5, 2 });
	CHECK_FALSE(exportpack::ReadSource(j.dump(), source, error));
	CHECK(source.mode == "kept");
}

TEST_CASE("Packs are listed by the manifest named after their folder")
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_pack_list_tests";
	std::filesystem::remove_all(folder);
	CHECK(exportpack::ListPacks(folder).empty());

	std::filesystem::create_directories(folder / "beta");
	std::filesystem::create_directories(folder / "alpha");
	std::filesystem::create_directories(folder / "no manifest");
	std::ofstream(folder / "beta" / "beta.json") << exportpack::ToJson(SampleManifest());
	std::ofstream(folder / "alpha" / "alpha.json") << exportpack::ToJson(SampleManifest());
	std::ofstream(folder / "no manifest" / "other.json") << "{}";
	std::ofstream(folder / "loose.json") << "{}";

	const auto packs = exportpack::ListPacks(folder);
	REQUIRE(packs.size() == 2);
	CHECK(packs[0].filename() == "alpha.json");
	CHECK(packs[1].filename() == "beta.json");

	exportpack::PackSource source;
	std::string error;
	CHECK_MESSAGE(exportpack::LoadSource(packs[0], source, error), error);
	CHECK_FALSE(exportpack::LoadSource(folder / "missing" / "missing.json", source, error));

	std::filesystem::remove_all(folder);
}

TEST_CASE("Unreal import: Z scale 100 spans 512 m around the actor")
{
	exportpack::Manifest m = SampleManifest();
	m.range = heightmap::noiseRange;
	m.metresPerUnit = 256.0f;	//-256 m to 256 m
	exportpack::UnrealImport u = exportpack::Unreal(m);
	CHECK(u.zScale == doctest::Approx(100.0f));
	CHECK(u.locationZ == doctest::Approx(0.0f));

	m.range = heightmap::terrainRange;	//0 m to 256 m
	u = exportpack::Unreal(m);
	CHECK(u.zScale == doctest::Approx(50.0f));
	CHECK(u.locationZ == doctest::Approx(12800.0f));
}
