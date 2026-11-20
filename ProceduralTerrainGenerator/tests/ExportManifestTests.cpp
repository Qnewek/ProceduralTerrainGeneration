#include "doctest/doctest.h"

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

TEST_CASE("Manifest embeds the generation parameters as JSON, or as text if they are not JSON")
{
	exportpack::Manifest m = SampleManifest();
	m.generation = R"({"version": 2, "seed": 7})";
	CHECK(Json::parse(exportpack::ToJson(m))["generation"]["seed"] == 7);

	m.generation = "not json";
	CHECK(Json::parse(exportpack::ToJson(m))["generation"] == "not json");
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
