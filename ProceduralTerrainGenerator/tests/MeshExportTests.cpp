#include "doctest/doctest.h"

#include <cmath>
#include <filesystem>
#include <string>

#include <cgltf.h>

#include "MeshExport.h"

static HeightField Valley(int width, int height)
{
	HeightField map(width, height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			map.At(x, y) = 0.3f + 0.2f * std::sin(x * 0.2f) * std::sin(y * 0.15f);
		}
	}
	return map;
}

//Parses and validates a .glb; nullptr on failure (free with cgltf_free)
static cgltf_data* LoadGlb(const std::filesystem::path& path)
{
	cgltf_options options{};
	cgltf_data* data = nullptr;
	if (cgltf_parse_file(&options, path.string().c_str(), &data) != cgltf_result_success) {
		return nullptr;
	}
	if (cgltf_load_buffers(&options, data, path.string().c_str()) != cgltf_result_success || cgltf_validate(data) != cgltf_result_success) {
		cgltf_free(data);
		return nullptr;
	}
	return data;
}

TEST_CASE("Mesh export uses the pixel size")
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_mesh_pixel_tests";
	std::filesystem::remove_all(folder);
	meshexport::Settings settings;
	settings.name = "dem";
	settings.lodLevels = 1;
	settings.tileMetres = 0.0f;
	settings.metresPerTexel = 30.0f;
	const meshexport::Result result = meshexport::Write(Valley(9, 5), settings, folder);
	REQUIRE_MESSAGE(result.ok, result.error);
	cgltf_data* data = LoadGlb(folder / result.levels[0].path);
	REQUIRE(data != nullptr);
	REQUIRE(data->nodes_count == 1);
	const cgltf_accessor* positions = data->nodes[0].mesh->primitives[0].attributes[0].data;
	CHECK(positions->max[0] == doctest::Approx(240.0f));
	CHECK(positions->max[2] == doctest::Approx(120.0f));
	cgltf_free(data);
	std::filesystem::remove_all(folder);
}

TEST_CASE("Mesh export writes a file per level of detail with the tiles, and the water")
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_mesh_export_tests";
	std::filesystem::remove_all(folder);
	const HeightField map = Valley(65, 41);

	meshexport::Settings settings;
	settings.name = "vale";
	settings.lodLevels = 3;
	settings.tileMetres = 32.0f;
	settings.metresPerUnit = 100.0f;
	settings.terrainTexture = "vale_biome_colors.png";
	settings.water = true;
	settings.seaLevel = 0.25f;
	settings.riverWater = { { 10, 40, 10 }, { 12, 40, 10 }, { 10, 40, 12 }, { 500, 40, 500 }, { 502, 40, 500 }, { 500, 40, 502 } };

	SUBCASE("glTF")
	{
		const meshexport::Result result = meshexport::Write(map, settings, folder);
		REQUIRE_MESSAGE(result.ok, result.error);
		REQUIRE(result.levels.size() == 3);
		CHECK(result.levels[0].triangles == 64u * 40u * 2u);
		CHECK(result.levels[0].errorMetres == 0.0f);
		for (size_t l = 1; l < result.levels.size(); l++) {
			CAPTURE(l);
			CHECK(result.levels[l].triangles < result.levels[l - 1].triangles);
			CHECK(result.levels[l].errorMetres >= 0.0f);
			CHECK(result.levels[l].errorMetres < 40.0f);
		}
		//64 x 40 m in squares of 32 m: 2 x 2 tiles (40 m reaches into a second row)
		CHECK(result.levels[0].tiles == 4);

		for (const exportpack::MeshLevel& level : result.levels) {
			CAPTURE(level.path);
			cgltf_data* data = LoadGlb(folder / level.path);
			REQUIRE(data != nullptr);
			CHECK(data->nodes_count == static_cast<size_t>(level.tiles));
			size_t triangles = 0;
			for (size_t n = 0; n < data->nodes_count; n++) {
				triangles += data->nodes[n].mesh->primitives[0].indices->count / 3;
				CHECK(std::string(data->nodes[n].name).find("_LOD" + std::to_string(level.lod)) != std::string::npos);
			}
			CHECK(triangles == level.triangles);
			REQUIRE(data->materials_count == 1);
			REQUIRE(data->materials[0].pbr_metallic_roughness.base_color_texture.texture != nullptr);
			CHECK(std::string(data->materials[0].pbr_metallic_roughness.base_color_texture.texture->image->uri) == "vale_biome_colors.png");
			cgltf_free(data);
		}

		REQUIRE(result.waterFile == "vale_water.glb");
		CHECK(result.waterNodes == std::vector<std::string>{ "sea", "rivers_lakes" });
		cgltf_data* water = LoadGlb(folder / result.waterFile);
		REQUIRE(water != nullptr);
		REQUIRE(water->nodes_count == 2);
		//The sea at 0.25 * 100 m over the whole map; the river triangle outside the map is dropped
		const cgltf_accessor* sea = water->nodes[0].mesh->primitives[0].attributes[0].data;
		CHECK(sea->max[1] == doctest::Approx(25.0f));
		CHECK(sea->max[0] == doctest::Approx(64.0f));
		CHECK(sea->max[2] == doctest::Approx(40.0f));
		CHECK(water->nodes[1].mesh->primitives[0].indices->count == 3);
		CHECK(water->materials[0].alpha_mode == cgltf_alpha_mode_blend);
		cgltf_free(water);

		CHECK(result.files.size() == 4);
		CHECK_FALSE(std::filesystem::exists(folder / "vale_terrain_lod0.glb.tmp"));
	}
	SUBCASE("OBJ, one tile, no water to write")
	{
		settings.format = meshexport::Format::OBJ;
		settings.tileMetres = 0.0f;
		settings.lodLevels = 2;
		settings.seaLevel.reset();
		settings.riverWater.clear();
		const meshexport::Result result = meshexport::Write(map, settings, folder);
		REQUIRE_MESSAGE(result.ok, result.error);
		REQUIRE(result.levels.size() == 2);
		CHECK(result.levels[0].tiles == 1);
		CHECK(result.levels[0].path == "vale_terrain_lod0.obj");
		CHECK(std::filesystem::file_size(folder / "vale_terrain_lod0.obj") > 0);
		CHECK(std::filesystem::exists(folder / "vale_terrain_lod1.obj"));
		CHECK(result.waterFile.empty());
		CHECK_FALSE(std::filesystem::exists(folder / "vale_water.obj"));
	}
	SUBCASE("A map smaller than 2 x 2 is an error")
	{
		const meshexport::Result result = meshexport::Write(HeightField(1, 1), settings, folder);
		CHECK_FALSE(result.ok);
		CHECK_FALSE(result.error.empty());
	}

	std::filesystem::remove_all(folder);
}
