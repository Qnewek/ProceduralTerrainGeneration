#include "doctest/doctest.h"

#include <cstring>
#include <sstream>
#include <string>

#include <cgltf.h>

#include "MeshFile.h"
#include "TerrainMesh.h"

static terrainmesh::Mesh SmallGrid()
{
	HeightField map(4, 3);
	for (size_t i = 0; i < map.Size(); i++) {
		map[i] = static_cast<float>(i) / 12.0f;
	}
	return terrainmesh::BuildGrid(map, 10.0f);
}

TEST_CASE("GLB is a valid glTF 2.0 file holding the meshes as they are")
{
	const terrainmesh::Mesh grid = SmallGrid();
	const terrainmesh::Mesh quad = terrainmesh::Quad({ 0.0f, 0.0f }, { 3.0f, 2.0f }, 1.5f);
	const terrainmesh::Mesh empty;
	std::vector<meshfile::Material> materials(2);
	materials[0].name = "terrain";
	materials[0].texture = "alps_biome_colors.png";
	materials[1].name = "water";
	materials[1].color = glm::vec4(0.1f, 0.3f, 0.5f, 0.7f);
	materials[1].blend = true;
	const std::vector<meshfile::Node> nodes = { { "terrain_0_0_LOD0", { { &grid, 0 } } }, { "empty", { { &empty, 0 } } }, { "sea", { { &quad, 1 } } } };

	const std::vector<unsigned char> glb = meshfile::EncodeGlb(nodes, materials, "test");
	REQUIRE(glb.size() % 4 == 0);

	cgltf_options options{};
	cgltf_data* data = nullptr;
	REQUIRE(cgltf_parse(&options, glb.data(), glb.size(), &data) == cgltf_result_success);
	CHECK(cgltf_load_buffers(&options, data, nullptr) == cgltf_result_success);
	CHECK(cgltf_validate(data) == cgltf_result_success);

	//The empty node is left out
	REQUIRE(data->nodes_count == 2);
	CHECK(std::string(data->nodes[0].name) == "terrain_0_0_LOD0");
	CHECK(std::string(data->nodes[1].name) == "sea");
	REQUIRE(data->scenes_count == 1);
	CHECK(data->scenes[0].nodes_count == 2);

	const cgltf_primitive& primitive = data->nodes[0].mesh->primitives[0];
	CHECK(primitive.type == cgltf_primitive_type_triangles);
	REQUIRE(primitive.indices != nullptr);
	CHECK(primitive.indices->count == grid.indices.size());
	int wrongIndex = 0;
	for (size_t i = 0; i < grid.indices.size(); i++) {
		wrongIndex += cgltf_accessor_read_index(primitive.indices, i) != grid.indices[i];
	}
	CHECK(wrongIndex == 0);

	bool position = false, normal = false, uv = false;
	for (size_t a = 0; a < primitive.attributes_count; a++) {
		const cgltf_attribute& attribute = primitive.attributes[a];
		int wrong = 0;
		for (size_t v = 0; v < grid.positions.size(); v++) {
			float value[3] = {};
			cgltf_accessor_read_float(attribute.data, v, value, 3);
			if (attribute.type == cgltf_attribute_type_position) {
				wrong += glm::vec3(value[0], value[1], value[2]) != grid.positions[v];
			}
			else if (attribute.type == cgltf_attribute_type_normal) {
				wrong += glm::vec3(value[0], value[1], value[2]) != grid.normals[v];
			}
			else if (attribute.type == cgltf_attribute_type_texcoord) {
				wrong += glm::vec2(value[0], value[1]) != grid.uvs[v];
			}
		}
		CHECK(wrong == 0);
		position |= attribute.type == cgltf_attribute_type_position;
		normal |= attribute.type == cgltf_attribute_type_normal;
		uv |= attribute.type == cgltf_attribute_type_texcoord;
		if (attribute.type == cgltf_attribute_type_position) {
			REQUIRE(attribute.data->has_max);
			CHECK(attribute.data->max[0] == 3.0f);
			CHECK(attribute.data->max[2] == 2.0f);
		}
	}
	CHECK(position);
	CHECK(normal);
	CHECK(uv);

	REQUIRE(data->materials_count == 2);
	REQUIRE(primitive.material == &data->materials[0]);
	const cgltf_texture* texture = data->materials[0].pbr_metallic_roughness.base_color_texture.texture;
	REQUIRE(texture != nullptr);
	CHECK(std::string(texture->image->uri) == "alps_biome_colors.png");
	CHECK(data->materials[1].alpha_mode == cgltf_alpha_mode_blend);
	CHECK(data->materials[1].pbr_metallic_roughness.base_color_factor[3] == doctest::Approx(0.7f));

	cgltf_free(data);
}

TEST_CASE("GLB without triangles is still valid")
{
	const std::vector<unsigned char> glb = meshfile::EncodeGlb({}, {}, "test");
	cgltf_options options{};
	cgltf_data* data = nullptr;
	REQUIRE(cgltf_parse(&options, glb.data(), glb.size(), &data) == cgltf_result_success);
	CHECK(cgltf_validate(data) == cgltf_result_success);
	CHECK(data->nodes_count == 0);
	cgltf_free(data);
}

TEST_CASE("OBJ has an object per node with 1-based indices running over the file and v flipped")
{
	const terrainmesh::Mesh a = terrainmesh::Quad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
	const terrainmesh::Mesh b = terrainmesh::Quad({ 2.0f, 0.0f }, { 3.0f, 1.0f }, 1.0f);
	const std::string obj = meshfile::EncodeObj({ { "first", { { &a, -1 } } }, { "second", { { &b, -1 } } } }, "test");

	std::istringstream lines(obj);
	std::string line;
	int objects = 0, v = 0, vt = 0, vn = 0, f = 0;
	std::vector<std::string> faces;
	std::vector<std::string> uvs;
	while (std::getline(lines, line)) {
		objects += line.rfind("o ", 0) == 0;
		v += line.rfind("v ", 0) == 0;
		vn += line.rfind("vn ", 0) == 0;
		if (line.rfind("vt ", 0) == 0) {
			vt++;
			uvs.push_back(line);
		}
		if (line.rfind("f ", 0) == 0) {
			f++;
			faces.push_back(line);
		}
	}
	CHECK(objects == 2);
	CHECK(v == 8);
	CHECK(vt == 8);
	CHECK(vn == 8);
	CHECK(f == 4);
	//Quad indices 0, 2, 1: the second object starts at vertex 5
	CHECK(faces[0] == "f 1/1/1 3/3/3 2/2/2");
	CHECK(faces[2] == "f 5/5/5 7/7/7 6/6/6");
	//uv (0, 0) is the top of the image: OBJ v = 1
	CHECK(uvs[0] == "vt 0 1");
}
