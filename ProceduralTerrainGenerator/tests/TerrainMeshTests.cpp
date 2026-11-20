#include "doctest/doctest.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <set>

#include "TerrainMesh.h"

//Smooth hills, non-square
static HeightField Hills(int width, int height)
{
	HeightField map(width, height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			map.At(x, y) = 0.5f + 0.25f * std::sin(x * 0.11f) * std::cos(y * 0.07f) + 0.1f * std::sin((x + 2 * y) * 0.31f);
		}
	}
	return map;
}

//Triangles not facing up, edge-on included (stricter than terrainmesh::Folds)
static int DownFacing(const terrainmesh::Mesh& mesh, const std::vector<uint32_t>& indices)
{
	int down = 0;
	for (size_t t = 0; t + 2 < indices.size(); t += 3) {
		const glm::vec3 a = mesh.positions[indices[t]], b = mesh.positions[indices[t + 1]], c = mesh.positions[indices[t + 2]];
		down += glm::cross(b - a, c - a).y <= 0.0f;
	}
	return down;
}

TEST_CASE("BuildGrid puts a vertex on every texel in metres, facing up")
{
	const int width = 7, height = 5;
	HeightField map = Hills(width, height);
	const terrainmesh::Mesh mesh = terrainmesh::BuildGrid(map, 256.0f);
	REQUIRE(mesh.positions.size() == map.Size());
	CHECK(mesh.normals.size() == map.Size());
	CHECK(mesh.uvs.size() == map.Size());
	CHECK(mesh.Triangles() == static_cast<size_t>((width - 1) * (height - 1) * 2));

	const glm::vec3 p = mesh.positions[3 * width + 4];
	CHECK(p.x == 4.0f);
	CHECK(p.z == 3.0f);
	CHECK(p.y == doctest::Approx(map.At(4, 3) * 256.0f));
	CHECK(mesh.uvs[3 * width + 4].x == doctest::Approx(4.0f / 6.0f));
	CHECK(mesh.uvs[3 * width + 4].y == doctest::Approx(3.0f / 4.0f));
	CHECK(mesh.uvs.back() == glm::vec2(1.0f, 1.0f));
	CHECK(DownFacing(mesh, mesh.indices) == 0);
}

TEST_CASE("BuildGrid normals follow the slope")
{
	//Height rises along +x by 2 m per texel: normal (-2, 1, 0) normalized
	HeightField ramp(4, 3);
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 4; x++) {
			ramp.At(x, y) = static_cast<float>(x);
		}
	}
	const terrainmesh::Mesh mesh = terrainmesh::BuildGrid(ramp, 2.0f);
	const glm::vec3 expected = glm::normalize(glm::vec3(-2.0f, 1.0f, 0.0f));
	int wrong = 0;
	for (const glm::vec3& n : mesh.normals) {
		wrong += glm::length(n - expected) > 1e-5f;
	}
	CHECK(wrong == 0);
}

TEST_CASE("BuildGrid of a map smaller than 2 x 2 is empty")
{
	CHECK(terrainmesh::BuildGrid(HeightField(1, 5), 1.0f).positions.empty());
}

TEST_CASE("Simplify reduces the triangles, keeps the vertices and has no folds")
{
	const HeightField map = Hills(65, 49);
	const terrainmesh::Mesh mesh = terrainmesh::BuildGrid(map, 100.0f);
	const std::vector<uint32_t> simple = terrainmesh::Simplify(mesh, mesh.indices, mesh.Triangles() / 8);
	CHECK(simple.size() / 3 <= mesh.Triangles() / 8 + 8);
	CHECK(simple.size() / 3 > 0);
	int outside = 0;
	for (uint32_t v : simple) {
		outside += v >= mesh.positions.size();
	}
	CHECK(outside == 0);
	CHECK(terrainmesh::Folds(mesh, simple) == 0);
	const float error = terrainmesh::MaxHeightError(mesh, simple, 65, 49);
	CHECK(error > 0.0f);
	CHECK(error < 35.0f);

	//A flat map simplifies to almost nothing with no height error
	const terrainmesh::Mesh flat = terrainmesh::BuildGrid(HeightField(33, 17), 100.0f);
	const std::vector<uint32_t> flatSimple = terrainmesh::Simplify(flat, flat.indices, 2);
	CHECK(flatSimple.size() / 3 <= 4);
	CHECK(terrainmesh::MaxHeightError(flat, flatSimple, 33, 17) == 0.0f);
}

TEST_CASE("MaxHeightError measures the texels under the triangles")
{
	//3 x 3 grid with a 6 m bump in the middle; two triangles over the corners miss it by 6 m
	HeightField map(3, 3);
	map.At(1, 1) = 1.0f;
	const terrainmesh::Mesh grid = terrainmesh::BuildGrid(map, 6.0f);
	CHECK(terrainmesh::MaxHeightError(grid, grid.indices, 3, 3) == 0.0f);
	const std::vector<uint32_t> corners = { 0, 6, 2, 2, 6, 8 };
	CHECK(terrainmesh::Folds(grid, corners) == 0);
	CHECK(terrainmesh::MaxHeightError(grid, corners, 3, 3) == doctest::Approx(6.0f));
}

TEST_CASE("Folds counts triangles turned over, not edge-on ones")
{
	terrainmesh::Mesh mesh;
	mesh.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 2, 5, 0 } };
	CHECK(terrainmesh::Folds(mesh, { 0, 2, 1 }) == 0);
	CHECK(terrainmesh::Folds(mesh, { 0, 1, 2 }) == 1);
	//Collinear seen from above: a vertical triangle
	CHECK(terrainmesh::Folds(mesh, { 0, 1, 3 }) == 0);
}

TEST_CASE("Simplify leaves no folds on cliffs")
{
	//Steps of 40 m between neighbouring texels: slopes of 40
	HeightField cliffs(48, 40);
	for (int y = 0; y < 40; y++) {
		for (int x = 0; x < 48; x++) {
			cliffs.At(x, y) = static_cast<float>(((x / 5) + (y / 7)) % 3) + 0.05f * std::sin(x * 1.3f + y * 0.7f);
		}
	}
	const terrainmesh::Mesh mesh = terrainmesh::BuildGrid(cliffs, 40.0f);
	std::vector<uint32_t> indices = mesh.indices;
	for (int lod = 1; lod <= 3; lod++) {
		indices = terrainmesh::Simplify(mesh, indices, mesh.Triangles() >> (2 * lod));
		CAPTURE(lod);
		CHECK(terrainmesh::Folds(mesh, indices) == 0);
	}
}

TEST_CASE("SplitTiles gives every triangle to one tile with its own vertices")
{
	const HeightField map = Hills(70, 45);
	const terrainmesh::Mesh mesh = terrainmesh::BuildGrid(map, 50.0f);
	const std::vector<terrainmesh::Tile> tiles = terrainmesh::SplitTiles(mesh, mesh.indices, 32.0f);
	//70 x 45 texels = 69 x 44 m: 3 x 2 squares of 32 m
	CHECK(tiles.size() == 6);

	size_t triangles = 0;
	std::set<std::pair<int, int>> keys;
	int outsideSquare = 0, badIndex = 0;
	for (const terrainmesh::Tile& tile : tiles) {
		triangles += tile.mesh.Triangles();
		keys.insert({ tile.x, tile.z });
		CHECK(tile.mesh.normals.size() == tile.mesh.positions.size());
		CHECK(tile.mesh.uvs.size() == tile.mesh.positions.size());
		for (size_t t = 0; t + 2 < tile.mesh.indices.size(); t += 3) {
			for (int k = 0; k < 3; k++) {
				badIndex += tile.mesh.indices[t + k] >= tile.mesh.positions.size();
			}
			if (badIndex == 0) {
				const glm::vec3 c = (tile.mesh.positions[tile.mesh.indices[t]] + tile.mesh.positions[tile.mesh.indices[t + 1]]
					+ tile.mesh.positions[tile.mesh.indices[t + 2]]) / 3.0f;
				outsideSquare += static_cast<int>(std::floor(c.x / 32.0f)) != tile.x || static_cast<int>(std::floor(c.z / 32.0f)) != tile.z;
			}
		}
	}
	CHECK(badIndex == 0);
	CHECK(outsideSquare == 0);
	CHECK(keys.size() == tiles.size());
	CHECK(triangles == mesh.Triangles());

	const std::vector<terrainmesh::Tile> one = terrainmesh::SplitTiles(mesh, mesh.indices, 0.0f);
	REQUIRE(one.size() == 1);
	CHECK(one[0].mesh.positions.size() == mesh.positions.size());
	CHECK(one[0].mesh.Triangles() == mesh.Triangles());
}

TEST_CASE("Quad faces up")
{
	const terrainmesh::Mesh quad = terrainmesh::Quad({ 0.0f, 0.0f }, { 10.0f, 4.0f }, 3.0f);
	CHECK(quad.Triangles() == 2);
	CHECK(DownFacing(quad, quad.indices) == 0);
	CHECK(quad.positions[3] == glm::vec3(10.0f, 3.0f, 4.0f));
}

TEST_CASE("FromTriangles merges vertices, turns triangles up and drops the ones outside")
{
	//Two triangles of a square sharing an edge, the second one clockwise; a third one outside the rectangle
	const std::vector<glm::vec3> soup = {
		{ 0, 1, 0 }, { 0, 1, 2 }, { 2, 1, 0 },
		{ 2, 1, 0 }, { 2, 1, 2 }, { 0, 1, 2 },
		{ 50, 1, 50 }, { 50, 1, 52 }, { 52, 1, 50 }
	};
	const terrainmesh::Mesh mesh = terrainmesh::FromTriangles(soup, { 0.0f, 0.0f }, { 10.0f, 10.0f });
	CHECK(mesh.Triangles() == 2);
	CHECK(mesh.positions.size() == 4);
	CHECK(DownFacing(mesh, mesh.indices) == 0);
	int wrong = 0;
	for (const glm::vec3& n : mesh.normals) {
		wrong += glm::length(n - glm::vec3(0.0f, 1.0f, 0.0f)) > 1e-5f;
	}
	CHECK(wrong == 0);
	CHECK(terrainmesh::FromTriangles({}, { 0.0f, 0.0f }, { 1.0f, 1.0f }).positions.empty());
}

//Timing of the mesh export of a 1024 x 1024 map; run with ptg_tests --no-skip -tc="*1024*"
TEST_CASE("Mesh export timing 1024 x 1024" * doctest::skip())
{
	using Clock = std::chrono::steady_clock;
	auto ms = [](Clock::time_point from) { return std::chrono::duration<double, std::milli>(Clock::now() - from).count(); };
	//Octaves of waves like fBm: mountains of about 150 m at 256 m per unit, slopes mostly under 1 with steeper spots
	HeightField map(1024, 1024);
	for (int y = 0; y < 1024; y++) {
		for (int x = 0; x < 1024; x++) {
			float h = 0.4f;
			for (int o = 0; o < 6; o++) {
				const float f = 0.006f * static_cast<float>(1 << o);
				h += 0.3f / static_cast<float>(1 << o) * std::sin(x * f + o * 1.7f) * std::cos(y * f * 1.3f + o * 0.9f);
			}
			map.At(x, y) = h;
		}
	}
	auto start = Clock::now();
	const terrainmesh::Mesh mesh = terrainmesh::BuildGrid(map, 256.0f);
	MESSAGE("grid: ", ms(start), " ms, ", mesh.Triangles(), " triangles");
	std::vector<uint32_t> indices = mesh.indices;
	for (int lod = 1; lod <= 3; lod++) {
		start = Clock::now();
		indices = terrainmesh::Simplify(mesh, indices, mesh.Triangles() >> (2 * lod));
		const double simplifyMs = ms(start);
		start = Clock::now();
		const float error = terrainmesh::MaxHeightError(mesh, indices, 1024, 1024);
		MESSAGE("LOD", lod, ": ", simplifyMs, " ms, ", indices.size() / 3, " tris, folds ", terrainmesh::Folds(mesh, indices), ", error ", error,
			" m (measured in ", ms(start), " ms)");
	}
	start = Clock::now();
	const std::vector<terrainmesh::Tile> tiles = terrainmesh::SplitTiles(mesh, mesh.indices, 256.0f);
	MESSAGE("tiles: ", ms(start), " ms, ", tiles.size());
}
