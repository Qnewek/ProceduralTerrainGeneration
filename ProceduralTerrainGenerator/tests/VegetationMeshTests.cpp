#include "doctest/doctest.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "VegetationMesh.h"

using namespace vegetation;

namespace {
	//count separate unit squares (2 triangles each) in a row, like leaf cards
	SourcePart Cards(int count)
	{
		SourcePart part;
		for (int i = 0; i < count; i++) {
			const uint32_t first = static_cast<uint32_t>(part.vertices.size());
			const float x = static_cast<float>(i) * 2.0f;
			for (const glm::vec2 corner : { glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0, 1), glm::vec2(1, 1) }) {
				Vertex v;
				v.position = glm::vec3(x + corner.x, corner.y, 0.0f);
				v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
				v.uv = corner;
				part.vertices.push_back(v);
			}
			part.indices.insert(part.indices.end(), { first, first + 1, first + 2, first + 2, first + 1, first + 3 });
		}
		return part;
	}

	//Wavy grid of n x n quads, a closed-free surface like a trunk or rock
	SourcePart Grid(int n)
	{
		SourcePart part;
		for (int z = 0; z <= n; z++) {
			for (int x = 0; x <= n; x++) {
				Vertex v;
				const float fx = static_cast<float>(x) / n, fz = static_cast<float>(z) / n;
				v.position = glm::vec3(fx, 0.1f * std::sin(fx * 6.0f) * std::cos(fz * 5.0f), fz);
				v.uv = glm::vec2(fx, fz);
				part.vertices.push_back(v);
			}
		}
		for (int z = 0; z < n; z++) {
			for (int x = 0; x < n; x++) {
				const uint32_t a = static_cast<uint32_t>(z * (n + 1) + x);
				const uint32_t b = a + 1, c = a + static_cast<uint32_t>(n + 1), d = c + 1;
				part.indices.insert(part.indices.end(), { a, c, b, b, c, d });
			}
		}
		return part;
	}

	float Area(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		float area = 0.0f;
		for (size_t i = 0; i + 2 < indices.size(); i += 3) {
			const glm::vec3 a = vertices[indices[i]].position, b = vertices[indices[i + 1]].position, c = vertices[indices[i + 2]].position;
			area += glm::length(glm::cross(b - a, c - a)) / 2.0f;
		}
		return area;
	}

	std::string Base64(const void* data, size_t size)
	{
		static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		const unsigned char* bytes = static_cast<const unsigned char*>(data);
		std::string out;
		for (size_t i = 0; i < size; i += 3) {
			const uint32_t chunk = (bytes[i] << 16) | ((i + 1 < size ? bytes[i + 1] : 0) << 8) | (i + 2 < size ? bytes[i + 2] : 0);
			out += alphabet[(chunk >> 18) & 63];
			out += alphabet[(chunk >> 12) & 63];
			out += i + 1 < size ? alphabet[(chunk >> 6) & 63] : '=';
			out += i + 2 < size ? alphabet[chunk & 63] : '=';
		}
		return out;
	}

	std::filesystem::path TempFile(const char* name)
	{
		return std::filesystem::temp_directory_path() / ("ptg_vegetation_test_" + std::string(name));
	}
}

TEST_CASE("IsFoliageName: leaves, twigs and needles in any case")
{
	CHECK(IsFoliageName("tree_small_02_leaves"));
	CHECK(IsFoliageName("Fir_Sapling_Twigs"));
	CHECK(IsFoliageName("pine NEEDLE"));
	CHECK_FALSE(IsFoliageName("tree_small_02_trunk"));
	CHECK_FALSE(IsFoliageName("rock_07"));
}

TEST_CASE("SimplifyCards keeps part of the cards, enlarged to the same area")
{
	const SourcePart part = Cards(200);
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	SimplifyCards(part, 100, vertices, indices);
	CHECK(indices.size() / 3 == 100);
	//Whole cards only: 4 vertices each
	CHECK(vertices.size() == 200);
	CHECK(Area(vertices, indices) == doctest::Approx(Area(part.vertices, part.indices)).epsilon(0.01));
	for (const uint32_t index : indices) {
		REQUIRE(index < vertices.size());
	}

	//Under the target: unchanged
	SimplifyCards(part, 1000, vertices, indices);
	CHECK(indices.size() == part.indices.size());
}

TEST_CASE("SimplifyCards enlarges the cards at most eight times")
{
	//One card of 100 kept: keeping the area would need 10 times, so the area grows only 8 x 8 times
	const SourcePart part = Cards(100);
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	SimplifyCards(part, 2, vertices, indices);
	CHECK(indices.size() / 3 == 2);
	CHECK(Area(vertices, indices) == doctest::Approx(64.0f));
}

TEST_CASE("SimplifySurface gets close to the target and keeps the shape")
{
	const SourcePart part = Grid(64);
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	SimplifySurface(part, 500, vertices, indices);
	const size_t triangles = indices.size() / 3;
	CHECK(triangles <= 750);
	CHECK(triangles >= 100);
	CHECK(Area(vertices, indices) == doctest::Approx(Area(part.vertices, part.indices)).epsilon(0.05));
	for (const uint32_t index : indices) {
		REQUIRE(index < vertices.size());
	}
}

TEST_CASE("MoveBaseToOrigin puts the lowest point at y = 0 and the base at x = z = 0")
{
	SourceModel model;
	SourcePart part = Grid(8);
	//Stand the grid up and move it away
	for (Vertex& v : part.vertices) {
		v.position = glm::vec3(v.position.x + 5.0f, v.position.z * 3.0f + 2.0f, v.position.y - 7.0f);
	}
	model.parts.push_back(part);
	MoveBaseToOrigin(model);
	float lowest = INFINITY, minX = INFINITY, maxX = -INFINITY;
	for (const Vertex& v : model.parts[0].vertices) {
		lowest = std::min(lowest, v.position.y);
		minX = std::min(minX, v.position.x);
		maxX = std::max(maxX, v.position.x);
	}
	CHECK(lowest == doctest::Approx(0.0f));
	CHECK(minX == doctest::Approx(-0.5f));
	CHECK(maxX == doctest::Approx(0.5f));
}

TEST_CASE("SimplifySurface thins out many small pieces, largest first kept")
{
	//300 small cards and one large one, as a surface (branches): the large one stays
	SourcePart part = Cards(300);
	const uint32_t first = static_cast<uint32_t>(part.vertices.size());
	for (const glm::vec2 corner : { glm::vec2(0, 0), glm::vec2(10, 0), glm::vec2(0, 10), glm::vec2(10, 10) }) {
		Vertex v;
		v.position = glm::vec3(corner.x, corner.y, 5.0f);
		part.vertices.push_back(v);
	}
	part.indices.insert(part.indices.end(), { first, first + 1, first + 2, first + 2, first + 1, first + 3 });
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	SimplifySurface(part, 50, vertices, indices);
	CHECK(indices.size() / 3 <= 75);
	bool largeKept = false;
	for (const Vertex& v : vertices) {
		largeKept |= v.position.z == 5.0f;
	}
	CHECK(largeKept);
}

TEST_CASE("SimplifySurface keeps a surface cut into strips by UV seams whole")
{
	//A standing grid whose rows are separate strips sharing positions but not vertices (UV seams), like a trunk:
	//it is one piece, so its bottom stays
	const int n = 64;
	const SourcePart grid = Grid(n);
	SourcePart part;
	for (int z = 0; z < n; z++) {
		const uint32_t first = static_cast<uint32_t>(part.vertices.size());
		for (int row = z; row <= z + 1; row++) {
			for (int x = 0; x <= n; x++) {
				Vertex v = grid.vertices[static_cast<size_t>(row * (n + 1) + x)];
				v.position = glm::vec3(v.position.x, v.position.z * 10.0f, v.position.y);
				part.vertices.push_back(v);
			}
		}
		for (int x = 0; x < n; x++) {
			const uint32_t a = first + static_cast<uint32_t>(x), b = a + 1, c = a + static_cast<uint32_t>(n + 1), d = c + 1;
			part.indices.insert(part.indices.end(), { a, c, b, b, c, d });
		}
	}
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	SimplifySurface(part, 200, vertices, indices);
	REQUIRE_FALSE(indices.empty());
	float lowest = INFINITY, highest = -INFINITY;
	for (const uint32_t index : indices) {
		lowest = std::min(lowest, vertices[index].position.y);
		highest = std::max(highest, vertices[index].position.y);
	}
	CHECK(lowest == doctest::Approx(0.0f).epsilon(0.01));
	CHECK(highest == doctest::Approx(10.0f).epsilon(0.01));
}

TEST_CASE("BuildLods: fewer triangles per level, foliage share, ranges inside the arrays, cache round trip")
{
	std::vector<Material> materials{ Material{ "trunk" }, Material{ "leaves" } };
	materials[1].foliage = true;
	SourceModel model;
	SourcePart trunk = Grid(40);	//3200 triangles
	SourcePart cards = Cards(400);	//800 triangles
	cards.material = 1;
	model.parts.push_back(trunk);
	model.parts.push_back(cards);
	SourceModel rock;
	rock.parts.push_back(Grid(10));	//200 triangles

	const ModelMesh mesh = BuildLods(materials, { model, rock }, { 10000, 1000, 100 });
	REQUIRE(mesh.variants.size() == 2);
	const Variant& tree = mesh.variants[0];
	REQUIRE(tree.lods.size() == 3);
	CHECK(tree.sourceTriangles == 4000);
	//The first level is over the size of the model: unchanged
	CHECK(tree.lods[0].triangles == 4000);
	CHECK(tree.lods[1].triangles < tree.lods[0].triangles);
	CHECK(tree.lods[2].triangles < tree.lods[1].triangles);
	CHECK(tree.lods[1].triangles <= 1500);
	//Foliage keeps foliageShare of a level although it has a fifth of the triangles
	CHECK(tree.lods[1].parts[1].indexCount / 3 == 600);
	CHECK(mesh.variants[1].lods[0].triangles == 200);
	for (const Variant& variant : mesh.variants) {
		for (const Lod& lod : variant.lods) {
			for (const Part& part : lod.parts) {
				REQUIRE(part.firstIndex + part.indexCount <= mesh.indices.size());
				for (uint32_t i = part.firstIndex; i < part.firstIndex + part.indexCount; i++) {
					REQUIRE(mesh.indices[i] < mesh.vertices.size());
				}
			}
		}
	}

	const std::filesystem::path file = TempFile("cache.bin");
	REQUIRE(SaveCache(file, mesh, 1234));
	ModelMesh loaded;
	REQUIRE(LoadCache(file, loaded, 1234));
	CHECK(loaded.vertices.size() == mesh.vertices.size());
	CHECK(loaded.indices == mesh.indices);
	REQUIRE(loaded.variants.size() == 2);
	CHECK(loaded.variants[0].lods.size() == 3);
	CHECK(loaded.variants[0].lods[2].triangles == tree.lods[2].triangles);
	CHECK(loaded.variants[1].boundsMax == mesh.variants[1].boundsMax);
	CHECK(loaded.materials[1].name == "leaves");
	CHECK(loaded.materials[1].foliage);
	CHECK(loaded.variants[0].sourceTriangles == 4000);
	CHECK(tree.sinkPerSlope == doctest::Approx(SinkPerSlope(model)));
	CHECK(tree.sinkPerSlope > 0.0f);
	CHECK(loaded.variants[0].sinkPerSlope == tree.sinkPerSlope);
	//Another key: the cache is stale
	CHECK_FALSE(LoadCache(file, loaded, 999));
	std::filesystem::remove(file);
	CHECK_FALSE(LoadCache(file, loaded, 1234));
}

TEST_CASE("SinkPerSlope: farthest reach over the low part of the model, branches higher up left out")
{
	SourceModel model;
	SourcePart part;
	auto ring = [&](float radius, float y) {
		for (int s = 0; s < 8; s++) {
			const float angle = 6.2831853f * s / 8;
			Vertex v;
			v.position = glm::vec3(radius * std::cos(angle), y, radius * std::sin(angle));
			part.vertices.push_back(v);
		}
	};
	//Lowest point at y = -2 (not yet moved to the origin): a 10 m trunk of radius 0.5, a root reaching 1.2 m out 0.4 m up,
	//a branch 3 m out halfway up (above the lowest tenth, so it does not count)
	ring(0.5f, -2.0f);
	ring(0.5f, 8.0f);
	ring(3.0f, 3.0f);
	Vertex root;
	root.position = glm::vec3(1.2f, -1.6f, 0.0f);
	part.vertices.push_back(root);
	model.parts.push_back(part);
	//The root tip 1.2 m out must reach down to ground 1.2 * slope lower, as far as on flat ground (0.4 m above it)
	CHECK(SinkPerSlope(model) == doctest::Approx(1.2f));

	//Nothing reaches out (a pole): no sinking
	SourceModel pole;
	SourcePart line;
	for (const float y : { 0.0f, 5.0f }) {
		Vertex v;
		v.position = glm::vec3(0.0f, y, 0.0f);
		line.vertices.push_back(v);
	}
	pole.parts.push_back(line);
	CHECK(SinkPerSlope(pole) == 0.0f);
	CHECK(SinkPerSlope(SourceModel{}) == 0.0f);
}

TEST_CASE("BuildLods keeps parts of fewer than 4 triangles whole")
{
	//The floor of 4 triangles per part must not exceed what a part has (std::clamp with lo > hi)
	std::vector<Material> materials{ Material{ "bark" }, Material{ "leaves" } };
	materials[1].foliage = true;
	SourceModel model;
	model.parts.push_back(Cards(1));	//2 triangles
	SourcePart leaves = Cards(1);
	leaves.material = 1;
	model.parts.push_back(leaves);

	const ModelMesh mesh = BuildLods(materials, { model }, { 4, 2, 1 });
	REQUIRE(mesh.variants.size() == 1);
	REQUIRE(mesh.variants[0].lods.size() == 3);
	for (const Lod& lod : mesh.variants[0].lods) {
		CHECK(lod.triangles == 4);
	}
}

namespace {
	//glTF with one triangle in the XZ plane used by the given nodes (JSON of the node list)
	std::string TriangleGltf(const std::string& nodes, const std::string& sceneNodes)
	{
		const float positions[9] = { 0, 0, 0, 0, 0, 1, 1, 0, 0 };
		return std::string(R"({
		"asset": { "version": "2.0" },
		"buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,)") + Base64(positions, sizeof(positions)) + R"(" } ],
		"bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
		"accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 0, 1] } ],
		"materials": [ { "name": "bark" } ],
		"meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "material": 0 } ] } ],
		"nodes": )" + nodes + R"(,
		"scenes": [ { "nodes": )" + sceneNodes + R"( } ]
	})";
	}

	//Loads a glTF given as text
	bool LoadText(const std::string& json, std::vector<Material>& materials, std::vector<SourceModel>& variants, std::string& error)
	{
		const std::filesystem::path file = TempFile("triangle.gltf");
		{
			std::ofstream out(file);
			out << json;
		}
		const bool loaded = LoadGltf(file, materials, variants, error);
		std::filesystem::remove(file);
		return loaded;
	}
}

TEST_CASE("LoadGltf applies node transforms, merges overlapping roots per material and computes missing normals")
{
	//Two roots: one moved up by 1, one mirrored in X; their footprints touch, so they are one model
	std::vector<Material> materials;
	std::vector<SourceModel> variants;
	std::string error;
	REQUIRE(LoadText(TriangleGltf(R"([ { "mesh": 0, "translation": [0, 1, 0] }, { "mesh": 0, "scale": [-1, 1, 1] } ])", "[0, 1]"), materials, variants, error));
	//The material of the file and one for primitives without a material
	REQUIRE(materials.size() == 2);
	CHECK(materials[0].name == "bark");
	CHECK_FALSE(materials[0].foliage);
	REQUIRE(variants.size() == 1);
	const SourceModel& model = variants[0];
	REQUIRE(model.parts.size() == 1);
	//Without texture coordinates in the file a vertex on the seam of the generated ones may be copied (CylinderUvs)
	CHECK(model.parts[0].vertices.size() >= 6);
	CHECK(model.TriangleCount() == 2);
	CHECK(model.parts[0].vertices[0].position.y == doctest::Approx(1.0f));
	CHECK(model.parts[0].vertices[model.parts[0].indices[4]].position.x == doctest::Approx(-1.0f));
	//Counter-clockwise from above in both copies (the mirrored one has its winding flipped): normals up
	for (const Vertex& v : model.parts[0].vertices) {
		CHECK(v.normal.y == doctest::Approx(1.0f));
	}

	std::vector<SourceModel> ignored;
	CHECK_FALSE(LoadGltf(TempFile("missing.gltf"), materials, ignored, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("LoadGltf gives texture coordinates to a primitive without them, not one point of the texture")
{
	//A constant UV has zero screen derivatives, so the tangent frame of the shader was NaN and the surface black (fir trunk_c)
	std::vector<Material> materials;
	std::vector<SourceModel> variants;
	std::string error;
	REQUIRE(LoadText(TriangleGltf(R"([ { "mesh": 0 } ])", "[0]"), materials, variants, error));
	REQUIRE(variants.size() == 1);
	const SourcePart& part = variants[0].parts[0];
	REQUIRE(part.indices.size() == 3);
	//A flat triangle around the axis: coordinates around it differ (a full map is checked on a trunk below)
	const glm::vec2 a = part.vertices[part.indices[0]].uv, b = part.vertices[part.indices[1]].uv, c = part.vertices[part.indices[2]].uv;
	CHECK(glm::length(b - a) + glm::length(c - a) > 0.1f);
}

TEST_CASE("CylinderUvs maps a leaning trunk: tiles around and up, no triangle across the seam")
{
	constexpr int segments = 16, rings = 21;
	constexpr float height = 10.0f, radius = 0.3f, lean = 0.05f;
	SourcePart part;
	//A vertex of an earlier primitive keeps its coordinates
	Vertex earlier;
	earlier.uv = glm::vec2(0.25f, 0.75f);
	part.vertices.push_back(earlier);
	//and its triangle keeps its indices
	part.indices.insert(part.indices.end(), { 0, 0, 0 });
	for (int k = 0; k < rings; k++) {
		const float y = height * k / (rings - 1);
		for (int s = 0; s < segments; s++) {
			//Half a step off, so no vertex lies exactly on the seam
			const float angle = 6.2831853f * (s + 0.5f) / segments;
			Vertex v;
			v.position = glm::vec3(lean * y + radius * std::cos(angle), y, radius * std::sin(angle));
			part.vertices.push_back(v);
		}
	}
	for (int k = 0; k + 1 < rings; k++) {
		for (int s = 0; s < segments; s++) {
			const uint32_t a = 1 + k * segments + s, b = 1 + k * segments + (s + 1) % segments;
			const uint32_t c = a + segments, d = b + segments;
			part.indices.insert(part.indices.end(), { a, c, b, b, c, d });
		}
	}
	const size_t vertexCount = part.vertices.size();
	CylinderUvs(part, 1, 3, 1.0f);

	CHECK(part.vertices[0].uv == earlier.uv);
	CHECK(part.indices[0] + part.indices[1] + part.indices[2] == 0u);
	part.indices.erase(part.indices.begin(), part.indices.begin() + 3);
	//Copies only on the seam: at most one per ring
	CHECK(part.vertices.size() > vertexCount);
	CHECK(part.vertices.size() <= vertexCount + rings);
	//Circumference 1.88 m in 1 m tiles = 2 tiles around
	const float tilesAround = 2.0f;
	const float stepU = tilesAround / segments, stepV = height / (rings - 1);
	float lowU = INFINITY, highU = -INFINITY, lowV = INFINITY, highV = -INFINITY;
	for (size_t i = 0; i < part.indices.size(); i += 3) {
		const glm::vec2 a = part.vertices[part.indices[i]].uv, b = part.vertices[part.indices[i + 1]].uv, c = part.vertices[part.indices[i + 2]].uv;
		CHECK(std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) > 1e-4f);
		CHECK(std::max({ a.x, b.x, c.x }) - std::min({ a.x, b.x, c.x }) < stepU * 1.2f);
		CHECK(std::max({ a.y, b.y, c.y }) - std::min({ a.y, b.y, c.y }) < stepV * 1.2f);
		for (const glm::vec2 uv : { a, b, c }) {
			lowU = std::min(lowU, uv.x);
			highU = std::max(highU, uv.x);
			lowV = std::min(lowV, uv.y);
			highV = std::max(highV, uv.y);
		}
	}
	CHECK(highU - lowU == doctest::Approx(tilesAround).epsilon(0.02));
	CHECK(highV - lowV == doctest::Approx(height).epsilon(0.02));
}

TEST_CASE("LoadGltf: roots apart are variants, children belong to their root")
{
	std::vector<Material> materials;
	std::vector<SourceModel> variants;
	std::string error;
	//Root 0 has child 2 (moved up, same footprint); root 1 stands 5 m away
	REQUIRE(LoadText(TriangleGltf(R"([ { "mesh": 0, "children": [2] }, { "mesh": 0, "translation": [5, 0, 0] }, { "mesh": 0, "translation": [0, 2, 0] } ])", "[0, 1]"),
		materials, variants, error));
	REQUIRE(variants.size() == 2);
	CHECK(variants[0].TriangleCount() == 2);
	CHECK(variants[1].TriangleCount() == 1);
	CHECK(variants[1].parts[0].vertices[0].position.x == doctest::Approx(5.0f));
}
