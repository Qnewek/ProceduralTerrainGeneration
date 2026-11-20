#include "doctest/doctest.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <tuple>

#include <cgltf.h>
#include <lodepng.h>
#include <nlohmann/json.hpp>

#include "FileIO.h"
#include "MeshFile.h"
#include "TerrainMesh.h"
#include "VegetationExport.h"

namespace {
	const biome::Range anywhere{ -1.0f, 1.0f };

	//A map of the given size with a flat height, no slope and one biome growing a tree, a bush and a stone
	struct TestWorld {
		HeightField height;
		std::vector<glm::vec4> surface;
		BiomeMap ids;
		vegexport::Input input;

		TestWorld(int width, int mapHeight) : height(width, mapHeight), surface(height.Size(), glm::vec4(0.0f)), ids(width, mapHeight)
		{
			for (size_t i = 0; i < height.Size(); i++) {
				height[i] = 0.5f;
				ids[i] = 0;
			}
			input.plants = { {
				{ "Tree", 0.5f, 1.0f, 2.0f, 30.0f, anywhere, 1.0f },
				{ "Bush", 0.2f, 1.0f, 1.0f, 40.0f, anywhere, 1.0f },
				{ "Stone", 0.2f, 2.0f, 3.0f, 70.0f, anywhere, 0.0f },
			} };
			input.models = { { "Tree", "tree", "tree_asset", 3, {} }, { "Bush", "shrub", "bush_asset", 1, {} }, { "Stone", "rock", "stone_asset", 2, {} } };
			input.settings.heightScale = 1024.0f;
			input.settings.waterHeight = 0.1f;
			input.settings.seed = 7;
			input.metresPerUnit = 256.0f;
		}
	};

	std::vector<vegetation::ModelInfo> Infos(const vegexport::Input& input)
	{
		std::vector<vegetation::ModelInfo> infos;
		for (const vegexport::Model& m : input.models) {
			infos.push_back({ m.name, m.variants, vegetation::LayerOfKind(m.kind) });
		}
		return infos;
	}

	std::filesystem::path TempFolder(const char* name)
	{
		const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("ptg_vegexport_test_" + std::string(name));
		std::filesystem::remove_all(folder);
		std::filesystem::create_directories(folder);
		return folder;
	}
}

TEST_CASE("PlaceMap places the plants of the app: the same cells as a chunk of the infinite world, in metres")
{
	//Not a multiple of the chunk size: the last chunks are cut by the map edge
	TestWorld world(300, 170);
	const std::vector<vegexport::Placed> placed = vegexport::PlaceMap(world.height, world.surface, world.ids, world.input);
	REQUIRE(placed.size() > 100);

	//Every plant on the map, standing on its height in metres, scale in metres per metre of the model
	int outside = 0, wrongHeight = 0, wrongScale = 0;
	for (const vegexport::Placed& p : placed) {
		outside += p.position.x < 0.0f || p.position.x > 299.0f || p.position.z < 0.0f || p.position.z > 169.0f;
		wrongHeight += p.position.y != doctest::Approx(0.5f * 256.0f);
		const float maxScale = p.model == 2 ? 3.0f : p.model == 0 ? 2.0f : 1.0f;
		wrongScale += p.scale < 0.99f || p.scale > maxScale + 0.01f;
	}
	CHECK(outside == 0);
	CHECK(wrongHeight == 0);
	CHECK(wrongScale == 0);

	//The chunk at texel (128, 0) as the app places it: world origin and units of the app, the same chunk maps
	constexpr int size = vegexport::chunkTexels + 3;
	std::vector<float> h(size * size, 0.5f);
	std::vector<glm::vec4> s(size * size, glm::vec4(0.0f));
	std::vector<int> ids(size * size, 0);
	vegetation::ChunkMaps maps;
	maps.size = size;
	maps.firstTexel = glm::ivec2(127, -1);
	maps.height = h.data();
	maps.surface = s.data();
	maps.ids = ids.data();
	vegetation::PlacementSettings settings = world.input.settings;
	settings.chunkTexel = glm::ivec2(128, 0);
	settings.chunkTexels = vegexport::chunkTexels;
	settings.mapOrigin = glm::vec2(-1000.0f, 512.0f);
	settings.worldUnitsPerTexel = 4.0f;
	std::vector<vegetation::Instance> app;
	vegetation::PlaceChunk(maps, world.input.plants, Infos(world.input), settings, app);
	REQUIRE(!app.empty());

	//Rounded to decimetres: the app adds its world origin, which rounds the position a little
	using Key = std::tuple<int, int, int, int>;
	std::set<Key> fromApp, fromExport;
	for (const vegetation::Instance& i : app) {
		const float x = (i.position.x - settings.mapOrigin.x) / 4.0f, z = (i.position.z - settings.mapOrigin.y) / 4.0f;
		fromApp.insert({ static_cast<int>(std::lround(x * 10.0f)), static_cast<int>(std::lround(z * 10.0f)), i.model, i.variant });
	}
	for (const vegexport::Placed& p : placed) {
		if (p.position.x >= 128.0f && p.position.x < 256.0f && p.position.z < 128.0f) {
			fromExport.insert({ static_cast<int>(std::lround(p.position.x * 10.0f)), static_cast<int>(std::lround(p.position.z * 10.0f)), p.model, p.variant });
		}
	}
	CHECK(fromApp == fromExport);

	//Deterministic
	const std::vector<vegexport::Placed> again = vegexport::PlaceMap(world.height, world.surface, world.ids, world.input);
	REQUIRE(again.size() == placed.size());
	CHECK(std::equal(placed.begin(), placed.end(), again.begin(), [](const vegexport::Placed& a, const vegexport::Placed& b) {
		return a.position == b.position && a.yaw == b.yaw && a.scale == b.scale && a.model == b.model && a.variant == b.variant;
	}));
}

TEST_CASE("PlaceMap of a map moved over the world gives the plants of the world there")
{
	TestWorld at0(300, 170);
	TestWorld moved(200, 150);
	moved.input.mapTexel = glm::ivec2(37, -50);
	const std::vector<vegexport::Placed> a = vegexport::PlaceMap(at0.height, at0.surface, at0.ids, at0.input);
	const std::vector<vegexport::Placed> b = vegexport::PlaceMap(moved.height, moved.surface, moved.ids, moved.input);
	REQUIRE(!b.empty());

	//In world map positions, centimetres; the overlap of both maps is x 37 - 236, z 0 - 99
	using Key = std::tuple<int, int, int, int>;
	auto inOverlap = [](glm::vec2 p) { return p.x >= 37.0f && p.x <= 236.0f && p.y >= 0.0f && p.y <= 99.0f; };
	std::set<Key> fromA, fromB;
	int outside = 0;
	for (const vegexport::Placed& p : a) {
		if (inOverlap({ p.position.x, p.position.z })) {
			fromA.insert({ static_cast<int>(std::lround(p.position.x * 100.0f)), static_cast<int>(std::lround(p.position.z * 100.0f)), p.model, p.variant });
		}
	}
	for (const vegexport::Placed& p : b) {
		outside += p.position.x < 0.0f || p.position.z < 0.0f || p.position.x > 199.0f || p.position.z > 149.0f;
		const glm::vec2 world(p.position.x + 37.0f, p.position.z - 50.0f);
		if (inOverlap(world)) {
			fromB.insert({ static_cast<int>(std::lround(world.x * 100.0f)), static_cast<int>(std::lround(world.y * 100.0f)), p.model, p.variant });
		}
	}
	CHECK(outside == 0);
	CHECK(fromA.size() > 50);
	CHECK(fromA == fromB);
}

TEST_CASE("PlaceMap leaves out water and maps that do not match")
{
	TestWorld world(140, 140);
	for (size_t i = 0; i < world.height.Size(); i++) {
		world.height[i] = 0.05f;	//below the water height
	}
	CHECK(vegexport::PlaceMap(world.height, world.surface, world.ids, world.input).empty());

	TestWorld other(140, 140);
	const BiomeMap smaller(100, 140);
	CHECK(vegexport::PlaceMap(other.height, other.surface, smaller, other.input).empty());
}

TEST_CASE("InstancesJson lists the models and one row per plant")
{
	TestWorld world(200, 200);
	const std::vector<vegexport::Placed> placed = vegexport::PlaceMap(world.height, world.surface, world.ids, world.input);
	std::vector<vegexport::ModelFile> files(world.input.models.size());
	files[0].path = "vegetation/tree_asset.glb";
	files[0].nodes = { { "Tree_0_LOD0", "Tree_0_LOD1" }, { "Tree_1_LOD0", "Tree_1_LOD1" }, { "Tree_2_LOD0", "Tree_2_LOD1" } };
	files[0].lodTriangles = { 1000, 250 };
	const std::string text = vegexport::InstancesJson(placed, world.input.models, files);

	const nlohmann::json j = nlohmann::json::parse(text);
	REQUIRE(j["models"].size() == 3);
	CHECK(j["models"][0]["name"] == "Tree");
	CHECK(j["models"][0]["layer"] == "Trees");
	CHECK(j["models"][0]["file"] == "vegetation/tree_asset.glb");
	CHECK(j["models"][0]["variantNodes"].size() == 3);
	CHECK(j["models"][0]["variantNodes"][1][1] == "Tree_1_LOD1");
	CHECK(j["models"][0]["lodTriangles"].size() == 2);
	CHECK_FALSE(j["models"][1].contains("file"));
	CHECK(j["columns"].size() == 7);
	REQUIRE(j["instances"].size() == placed.size());
	size_t counted = 0;
	for (const auto& m : j["models"]) {
		counted += m["count"].get<size_t>();
	}
	CHECK(counted == placed.size());
	const auto& row = j["instances"][0];
	REQUIRE(row.size() == 7);
	//Centimetres, yaw to 4 decimals
	CHECK(std::abs(row[0].get<float>() - placed[0].position.x) <= 0.0051f);
	CHECK(std::abs(row[2].get<float>() - placed[0].position.z) <= 0.0051f);
	CHECK(std::abs(row[3].get<float>() - placed[0].yaw) <= 0.00006f);
	CHECK(row[5].get<int>() == placed[0].model);
	CHECK(row[6].get<int>() == placed[0].variant);

	//No plants: still valid JSON
	const nlohmann::json none = nlohmann::json::parse(vegexport::InstancesJson({}, world.input.models, {}));
	CHECK(none["instances"].empty());
}

TEST_CASE("WriteModel writes every variant as a node with the materials and their textures")
{
	const std::filesystem::path source = TempFolder("source");
	const std::filesystem::path pack = TempFolder("pack");

	//A source model: two quads far apart = two variants, textured with a PNG next to the file
	const std::vector<unsigned char> pixels(4 * 4 * 3, 128);
	std::vector<unsigned char> png;
	REQUIRE(lodepng::encode(png, pixels, 4, 4, LCT_RGB, 8) == 0);
	std::string error;
	REQUIRE(WriteFileAtomic(source / "bark.png", png.data(), png.size(), error));
	const terrainmesh::Mesh a = terrainmesh::Quad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
	const terrainmesh::Mesh b = terrainmesh::Quad({ 10.0f, 0.0f }, { 11.0f, 1.0f }, 0.0f);
	std::vector<meshfile::Material> materials(1);
	materials[0].name = "bark";
	materials[0].texture = "bark.png";
	const std::vector<unsigned char> glb = meshfile::EncodeGlb({ { "a", { { &a, 0 } } }, { "b", { { &b, 0 } } } }, materials, "test");
	REQUIRE(WriteFileAtomic(source / "tree.glb", glb.data(), glb.size(), error));

	const vegexport::Model model{ "Tree", "tree", "tree", 2, source / "tree.glb" };
	const vegexport::ModelFile file = vegexport::WriteModel(model, pack);
	REQUIRE_MESSAGE(file.path == "vegetation/tree.glb", file.error);
	//Without levels of detail: one level, the source mesh
	REQUIRE(file.nodes.size() == 2);
	REQUIRE(file.nodes[0].size() == 1);
	CHECK(file.nodes[0][0] == "Tree_0_LOD0");
	CHECK(file.lodTriangles == std::vector<uint32_t>{ 2 });
	CHECK(std::filesystem::exists(pack / "vegetation" / "textures" / "bark.png"));

	const std::filesystem::path written = pack / file.path;
	cgltf_options options{};
	cgltf_data* data = nullptr;
	REQUIRE(cgltf_parse_file(&options, written.string().c_str(), &data) == cgltf_result_success);
	CHECK(cgltf_load_buffers(&options, data, written.string().c_str()) == cgltf_result_success);
	CHECK(cgltf_validate(data) == cgltf_result_success);
	REQUIRE(data->nodes_count == 2);
	CHECK(std::string(data->nodes[1].name) == "Tree_1_LOD0");
	REQUIRE(data->materials_count >= 1);
	const cgltf_texture* texture = data->materials[0].pbr_metallic_roughness.base_color_texture.texture;
	REQUIRE(texture != nullptr);
	CHECK(std::string(texture->image->uri) == "textures/bark.png");
	//Models repeat their textures
	REQUIRE(texture->sampler != nullptr);
	CHECK(texture->sampler->wrap_s == 10497);
	//Each variant stands on the origin
	const cgltf_accessor* positions = data->nodes[1].mesh->primitives[0].attributes[0].data;
	REQUIRE(positions->has_min);
	CHECK(positions->min[1] == doctest::Approx(0.0f));
	CHECK(positions->min[0] < 0.0f);
	CHECK(positions->max[0] > 0.0f);
	cgltf_free(data);

	//A missing source: not written, with the reason
	const vegexport::ModelFile missing = vegexport::WriteModel({ "Gone", "tree", "gone", 1, source / "gone.gltf" }, pack);
	CHECK(missing.path.empty());
	CHECK_FALSE(missing.error.empty());

	std::filesystem::remove_all(source);
	std::filesystem::remove_all(pack);
}

TEST_CASE("GLB keeps several primitives per node and the material options of models")
{
	const terrainmesh::Mesh a = terrainmesh::Quad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
	const terrainmesh::Mesh b = terrainmesh::Quad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, 1.0f);
	std::vector<meshfile::Material> materials(2);
	materials[0].name = "bark";
	materials[0].texture = "bark.png";
	materials[0].normalTexture = "bark_nor.png";
	materials[0].ormTexture = "bark_arm.png";
	materials[0].repeat = true;
	materials[1].name = "leaves";
	materials[1].texture = "leaves.png";
	materials[1].mask = true;
	materials[1].doubleSided = true;
	materials[1].repeat = true;
	const std::vector<unsigned char> glb = meshfile::EncodeGlb({ { "tree", { { &a, 0 }, { &b, 1 } } } }, materials, "test");

	cgltf_options options{};
	cgltf_data* data = nullptr;
	REQUIRE(cgltf_parse(&options, glb.data(), glb.size(), &data) == cgltf_result_success);
	CHECK(cgltf_load_buffers(&options, data, nullptr) == cgltf_result_success);
	CHECK(cgltf_validate(data) == cgltf_result_success);
	REQUIRE(data->nodes_count == 1);
	REQUIRE(data->nodes[0].mesh->primitives_count == 2);
	CHECK(data->nodes[0].mesh->primitives[1].material == &data->materials[1]);
	const cgltf_material& bark = data->materials[0];
	REQUIRE(bark.normal_texture.texture != nullptr);
	CHECK(std::string(bark.normal_texture.texture->image->uri) == "bark_nor.png");
	REQUIRE(bark.occlusion_texture.texture != nullptr);
	CHECK(bark.occlusion_texture.texture == bark.pbr_metallic_roughness.metallic_roughness_texture.texture);
	const cgltf_material& leaves = data->materials[1];
	CHECK(leaves.alpha_mode == cgltf_alpha_mode_mask);
	CHECK(leaves.alpha_cutoff == doctest::Approx(0.5f));
	CHECK(leaves.double_sided);
	//One image per file
	CHECK(data->images_count == 4);
	cgltf_free(data);
}

//Writes every downloaded model of res/vegetation (tools/fetch_models.py) and validates the files; run with
//ptg_tests --no-skip -tc="*downloaded*"
TEST_CASE("Vegetation export of the downloaded models" * doctest::skip())
{
	const std::filesystem::path resources = std::filesystem::path(__FILE__).parent_path().parent_path() / "res" / "vegetation";
	std::ifstream in(resources / "vegetation_models.json");
	REQUIRE(in);
	const nlohmann::json list = nlohmann::json::parse(in);
	const std::filesystem::path pack = TempFolder("downloaded");
	for (const auto& entry : list["models"]) {
		const std::string asset = entry["asset"].get<std::string>();
		const std::string kind = entry["kind"].get<std::string>();
		const std::vector<uint32_t> lods = entry.contains("lods") ? entry["lods"] : list["lodTriangles"][kind];
		const vegexport::Model model{ entry["name"].get<std::string>(), kind, asset, 0, resources / "models" / asset / (asset + ".gltf"), lods,
			resources / "cache" / (asset + ".mesh") };
		if (!std::filesystem::exists(model.source)) {
			MESSAGE(model.name, ": not downloaded");
			continue;
		}
		const auto start = std::chrono::steady_clock::now();
		const vegexport::ModelFile file = vegexport::WriteModel(model, pack);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		REQUIRE_MESSAGE(!file.path.empty(), model.name, ": ", file.error);
		const std::filesystem::path written = pack / file.path;
		cgltf_options options{};
		cgltf_data* data = nullptr;
		REQUIRE(cgltf_parse_file(&options, written.string().c_str(), &data) == cgltf_result_success);
		CHECK(cgltf_load_buffers(&options, data, written.string().c_str()) == cgltf_result_success);
		CHECK(cgltf_validate(data) == cgltf_result_success);
		int missingImages = 0;
		for (size_t i = 0; i < data->images_count; i++) {
			missingImages += !std::filesystem::exists(written.parent_path() / data->images[i].uri);
		}
		CHECK(missingImages == 0);
		MESSAGE(model.name, ": ", file.nodes.size(), " variants, LOD0 ", file.lodTriangles[0], " tris, ", data->nodes_count, " nodes, ",
			data->materials_count, " materials, ", data->images_count, " images, ",
			std::filesystem::file_size(written) / 1024, " KiB, ", ms, " ms");
		cgltf_free(data);
	}
	std::filesystem::remove_all(pack);
}
