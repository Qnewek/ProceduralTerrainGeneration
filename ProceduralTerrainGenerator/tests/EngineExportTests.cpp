#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "EngineExport.h"

namespace {
	std::filesystem::path EngineTempFolder(const char* name)
	{
		const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("ptg_engineexport_test_" + std::string(name));
		std::filesystem::remove_all(folder);
		std::filesystem::create_directories(folder);
		return folder;
	}

	void WriteText(const std::filesystem::path& path, const std::string& text)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary) << text;
	}

	engineexport::MaterialTextures Source(const std::filesystem::path& folder, float tileMetres, float r, float g, float b)
	{
		engineexport::MaterialTextures source;
		source.folder = folder;
		source.tileMetres = tileMetres;
		source.fallbackColor[0] = r;
		source.fallbackColor[1] = g;
		source.fallbackColor[2] = b;
		return source;
	}

	exportpack::Material OnMap(int id, float coverage)
	{
		exportpack::Material material;
		material.id = id;
		material.name = "material " + std::to_string(id);
		material.coverage = coverage;
		return material;
	}
}

TEST_CASE("Material textures: copied for materials on the map")
{
	const std::filesystem::path resources = EngineTempFolder("textures_res");
	const std::filesystem::path pack = EngineTempFolder("textures_pack");
	for (const char* asset : { "Grass004", "Rock030" }) {
		WriteText(resources / asset / "albedo.jpg", "albedo");
		WriteText(resources / asset / "normal.png", "normal");
		WriteText(resources / asset / "roughness.jpg", "roughness");
		WriteText(resources / asset / "height.jpg", "height");
	}
	const std::vector<engineexport::MaterialTextures> sources = { Source(resources / "Grass004", 1.0f, 0.3f, 0.4f, 0.15f),
		Source(resources / "Rock030", 1.5f, 0.42f, 0.4f, 0.36f) };
	std::vector<exportpack::Material> materials = { OnMap(0, 0.5f), OnMap(1, 0.0f) };
	std::vector<exportpack::File> files;
	std::string error;
	REQUIRE(engineexport::CopyMaterialTextures(sources, materials, pack, files, error));

	CHECK(materials[0].albedo == "materials/Grass004/albedo.jpg");
	CHECK(materials[0].normal == "materials/Grass004/normal.png");
	CHECK(materials[0].roughness == "materials/Grass004/roughness.jpg");
	CHECK(std::filesystem::exists(pack / materials[0].albedo));
	CHECK(std::filesystem::exists(pack / materials[0].normal));
	CHECK(std::filesystem::exists(pack / materials[0].roughness));
	//Not on the map: no files
	CHECK(materials[1].albedo.empty());
	CHECK(materials[1].normal.empty());
	CHECK_FALSE(std::filesystem::exists(pack / "materials" / "Rock030"));
	CHECK(files.size() == 3);
	CHECK(files[0].content == "material_albedo");
	CHECK(files[0].format == "jpg");
	//Repeat and fallback color on every material
	CHECK(materials[0].tileMetres == doctest::Approx(1.0f));
	CHECK(materials[1].tileMetres == doctest::Approx(1.5f));
	CHECK(materials[0].fallbackColor[1] == doctest::Approx(0.4f));
	CHECK(materials[1].fallbackColor[0] == doctest::Approx(0.42f));
}

TEST_CASE("Material textures: missing files keep the fallback color")
{
	const std::filesystem::path pack = EngineTempFolder("textures_missing");
	const std::vector<engineexport::MaterialTextures> sources = { Source(pack / "not_downloaded", 2.0f, 0.9f, 0.92f, 0.95f) };
	std::vector<exportpack::Material> materials = { OnMap(0, 1.0f) };
	std::vector<exportpack::File> files;
	std::string error;
	REQUIRE(engineexport::CopyMaterialTextures(sources, materials, pack, files, error));
	CHECK(materials[0].albedo.empty());
	CHECK(materials[0].normal.empty());
	CHECK(materials[0].roughness.empty());
	CHECK(files.empty());
	CHECK(materials[0].fallbackColor[0] == doctest::Approx(0.9f));
	CHECK(materials[0].fallbackColor[2] == doctest::Approx(0.95f));
	CHECK(materials[0].tileMetres == doctest::Approx(2.0f));
}

TEST_CASE("Material repeat in metres: texels of 4 world units, times the pixel size")
{
	//TerrainGen: tileSize 4 world units = 1 texel = 1 m
	CHECK(engineexport::TileMetres(4.0f, 1.0f, 1.0f) == doctest::Approx(1.0f));
	CHECK(engineexport::TileMetres(15.0f, 2.0f, 1.0f) == doctest::Approx(7.5f));
	//Imported map of 30 m pixels: materials repeat in texels
	CHECK(engineexport::TileMetres(4.0f, 1.0f, 30.0f) == doctest::Approx(30.0f));
}

TEST_CASE("Engine scripts: copied recursively")
{
	const std::filesystem::path source = EngineTempFolder("scripts_res");
	const std::filesystem::path pack = EngineTempFolder("scripts_pack");
	WriteText(source / "unity" / "Editor" / "A.cs", "class A {}");
	WriteText(source / "godot" / "addons" / "ptg_import" / "plugin.cfg", "[plugin]");
	std::vector<exportpack::File> files;
	std::string error;
	REQUIRE(engineexport::CopyEngineScripts(source, pack, files, error));
	CHECK(std::filesystem::exists(pack / "engine" / "unity" / "Editor" / "A.cs"));
	CHECK(std::filesystem::exists(pack / "engine" / "godot" / "addons" / "ptg_import" / "plugin.cfg"));
	REQUIRE(files.size() == 2);
	for (const exportpack::File& file : files) {
		CHECK(file.content == "engine_script");
		CHECK(file.path.rfind("engine/", 0) == 0);
	}

	std::vector<exportpack::File> none;
	CHECK_FALSE(engineexport::CopyEngineScripts(source / "missing", pack, none, error));
	CHECK(error.find("Import scripts not found") != std::string::npos);
}
