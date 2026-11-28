#pragma once

#include <cstdint>
#include <vector>

#include "glm/glm.hpp"

#include "Grid.h"

//Triangle meshes of the export pack: the terrain as a grid of the height map (one vertex per texel), its simplified levels of detail,
//tiles, and the water surfaces. Units are metres, Y up, right-handed as in the app: x = map column, z = map row (row 0 = the smallest z),
//texel (0, 0) at the origin. No OpenGL here.
namespace terrainmesh {
	struct Mesh {
		std::vector<glm::vec3> positions{};
		std::vector<glm::vec3> normals{};
		std::vector<glm::vec2> uvs{};			//u = column / (width - 1), v = row / (height - 1): the maps of the pack as textures, row 0 at v = 0
		std::vector<uint32_t> indices{};		//triangles, counter-clockwise seen from above (+Y)

		size_t Triangles() const { return indices.size() / 3; }
	};

	//The full grid: a vertex per texel, normals from central differences of the heights
	//@param metresPerUnit - metres of height value 1
	Mesh BuildGrid(const HeightField& map, float metresPerUnit, float metresPerTexel = 1.0f);

	//Simplified triangles over the same vertices (meshoptimizer, quadric error) without folds (see Folds); the map border keeps its outline
	//@param indices - triangles of mesh to simplify (e.g. the previous level of detail)
	//@param targetTriangles - aimed at; a mesh that cannot get that simple keeps more
	std::vector<uint32_t> Simplify(const Mesh& mesh, const std::vector<uint32_t>& indices, size_t targetTriangles);

	//Triangles turned clockwise seen from above, which overlap others: overhangs a height field must not have. Edge-on (vertical)
	//triangles are not counted, they cover nothing
	size_t Folds(const Mesh& mesh, const std::vector<uint32_t>& indices);

	//Largest difference between the heights of the grid texels and the surface of the triangles above them (metres)
	//@param grid - the full grid (BuildGrid) whose vertices the triangles use
	float MaxHeightError(const Mesh& grid, const std::vector<uint32_t>& indices, int width, int height, float metresPerTexel = 1.0f);

	//Part of a mesh in one square of the XZ plane
	struct Tile {
		int x = 0, z = 0;	//index of the square
		Mesh mesh{};
	};
	//Splits triangles into squares of tileMetres by the centre of each triangle, so no triangle is cut and the tiles meet without cracks.
	//Each tile keeps only its vertices. Empty squares are left out
	//@param tileMetres - <= 0 = one tile with everything
	std::vector<Tile> SplitTiles(const Mesh& mesh, const std::vector<uint32_t>& indices, float tileMetres);

	//Horizontal rectangle at height y facing up, UVs 0 - 1 over it
	Mesh Quad(glm::vec2 min, glm::vec2 max, float y);

	//Indexed mesh from separate triangles (3 vertices each): equal positions are merged, normals are the area-weighted face normals,
	//turned up; UVs map the rectangle uvMin - uvMax of the XZ plane to 0 - 1. Triangles whose centre lies outside the rectangle are dropped
	Mesh FromTriangles(const std::vector<glm::vec3>& vertices, glm::vec2 uvMin, glm::vec2 uvMax);
}
