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
	m.files = { { "alps_height.png", "height", heightmap::Format::PNG16 }, { "alps_height.exr", "height", heightmap::Format::EXR32 } };
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

TEST_CASE("Manifest without a sea or generation parameters leaves them out")
{
	exportpack::Manifest m = SampleManifest();
	m.seaLevel.reset();
	const Json j = Json::parse(exportpack::ToJson(m));
	CHECK_FALSE(j.contains("seaLevel"));
	CHECK_FALSE(j.contains("generation"));
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
