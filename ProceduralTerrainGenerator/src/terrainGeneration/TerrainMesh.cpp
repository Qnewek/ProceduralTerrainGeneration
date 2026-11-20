#include "TerrainMesh.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include <meshoptimizer.h>

namespace terrainmesh {
	Mesh BuildGrid(const HeightField& map, float metresPerUnit, float metresPerTexel)
	{
		Mesh mesh;
		const int width = map.GetWidth(), height = map.GetHeight();
		if (width < 2 || height < 2) {
			return mesh;
		}
		const size_t count = map.Size();
		mesh.positions.resize(count);
		mesh.normals.resize(count);
		mesh.uvs.resize(count);
		auto heightAt = [&](int x, int y) {
			const float h = map.At(std::clamp(x, 0, width - 1), std::clamp(y, 0, height - 1));
			return std::isfinite(h) ? h * metresPerUnit : 0.0f;
		};
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const size_t i = static_cast<size_t>(y) * width + x;
				mesh.positions[i] = glm::vec3(x * metresPerTexel, heightAt(x, y), y * metresPerTexel);
				mesh.uvs[i] = glm::vec2(static_cast<float>(x) / (width - 1), static_cast<float>(y) / (height - 1));
				//One-sided differences at the border (the clamped neighbour is the texel itself)
				const float dx = static_cast<float>(std::min(x + 1, width - 1) - std::max(x - 1, 0)) * metresPerTexel;
				const float dz = static_cast<float>(std::min(y + 1, height - 1) - std::max(y - 1, 0)) * metresPerTexel;
				const float slopeX = (heightAt(x + 1, y) - heightAt(x - 1, y)) / dx;
				const float slopeZ = (heightAt(x, y + 1) - heightAt(x, y - 1)) / dz;
				mesh.normals[i] = glm::normalize(glm::vec3(-slopeX, 1.0f, -slopeZ));
			}
		}

		mesh.indices.reserve(static_cast<size_t>(width - 1) * (height - 1) * 6);
		for (int y = 0; y + 1 < height; y++) {
			for (int x = 0; x + 1 < width; x++) {
				const uint32_t a = static_cast<uint32_t>(y * width + x);
				const uint32_t b = a + 1;
				const uint32_t c = a + static_cast<uint32_t>(width);
				const uint32_t d = c + 1;
				//(a, c, b) and (b, c, d) are counter-clockwise seen from +Y with x right and z down the map
				mesh.indices.insert(mesh.indices.end(), { a, c, b, b, c, d });
			}
		}
		return mesh;
	}

	size_t Folds(const Mesh& mesh, const std::vector<uint32_t>& indices)
	{
		size_t folds = 0;
		for (size_t t = 0; t + 2 < indices.size(); t += 3) {
			const glm::vec3 a = mesh.positions[indices[t]], b = mesh.positions[indices[t + 1]], c = mesh.positions[indices[t + 2]];
			folds += glm::cross(b - a, c - a).y < 0.0f;
		}
		return folds;
	}

	std::vector<uint32_t> Simplify(const Mesh& mesh, const std::vector<uint32_t>& indices, size_t targetTriangles)
	{
		if (indices.empty() || mesh.positions.empty()) {
			return indices;
		}
		//meshoptimizer rejects a collapse only when it turns a triangle normal by more than 90 degrees, so on a steep slope a triangle
		//could tip over the vertical into a fold. It simplifies a copy with the heights squeezed until the steepest triangle has a slope
		//of 0.25, so a fold needs a large turn; if one is left anyway, again squeezed 4 times more. The result indexes the same vertices,
		//so the heights stay exact. Measured on a 1024^2 map with slopes up to 40: 39 folds at slope 0.5, none after one more attempt
		float steepest = 0.0f;
		for (size_t t = 0; t + 2 < indices.size(); t += 3) {
			const glm::vec3 a = mesh.positions[indices[t]], b = mesh.positions[indices[t + 1]], c = mesh.positions[indices[t + 2]];
			const glm::vec3 n = glm::cross(b - a, c - a);
			if (n.y > 0.0f) {
				steepest = std::max(steepest, std::sqrt(n.x * n.x + n.z * n.z) / n.y);
			}
		}
		float squeeze = steepest > 0.25f ? 0.25f / steepest : 1.0f;

		std::vector<glm::vec3> squeezed(mesh.positions.size());
		std::vector<uint32_t> result(indices.size());
		const size_t target = std::min(targetTriangles * 3, indices.size());
		for (int attempt = 0; attempt < 3; attempt++, squeeze *= 0.25f) {
			for (size_t v = 0; v < squeezed.size(); v++) {
				squeezed[v] = glm::vec3(mesh.positions[v].x, mesh.positions[v].y * squeeze, mesh.positions[v].z);
			}
			result.resize(indices.size());
			//No error limit: the target count decides
			const size_t count = meshopt_simplify(result.data(), indices.data(), indices.size(), &squeezed[0].x, squeezed.size(), sizeof(glm::vec3),
				target, std::numeric_limits<float>::max(), meshopt_SimplifyErrorAbsolute, nullptr);
			result.resize(count);
			if (Folds(mesh, result) == 0) {
				break;
			}
		}
		meshopt_optimizeVertexCache(result.data(), result.data(), result.size(), mesh.positions.size());
		return result;
	}

	float MaxHeightError(const Mesh& grid, const std::vector<uint32_t>& indices, int width, int height, float metresPerTexel)
	{
		float worst = 0.0f;
		for (size_t t = 0; t + 2 < indices.size(); t += 3) {
			const glm::vec3 a = grid.positions[indices[t]], b = grid.positions[indices[t + 1]], c = grid.positions[indices[t + 2]];
			//Twice the area seen from above; edge-on triangles cover no texel
			const float area = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
			if (std::abs(area) < 1e-12f) {
				continue;
			}
			const int x0 = std::max(0, static_cast<int>(std::ceil(std::min({ a.x, b.x, c.x }) / metresPerTexel)));
			const int x1 = std::min(width - 1, static_cast<int>(std::floor(std::max({ a.x, b.x, c.x }) / metresPerTexel)));
			const int z0 = std::max(0, static_cast<int>(std::ceil(std::min({ a.z, b.z, c.z }) / metresPerTexel)));
			const int z1 = std::min(height - 1, static_cast<int>(std::floor(std::max({ a.z, b.z, c.z }) / metresPerTexel)));
			for (int z = z0; z <= z1; z++) {
				for (int x = x0; x <= x1; x++) {
					const float px = x * metresPerTexel, pz = z * metresPerTexel;
					//Barycentric weights of b and c; texels on an edge belong to both triangles
					const float wb = ((px - a.x) * (c.z - a.z) - (c.x - a.x) * (pz - a.z)) / area;
					const float wc = ((b.x - a.x) * (pz - a.z) - (px - a.x) * (b.z - a.z)) / area;
					const float eps = -1e-5f;
					if (wb < eps || wc < eps || 1.0f - wb - wc < eps) {
						continue;
					}
					const float y = a.y + wb * (b.y - a.y) + wc * (c.y - a.y);
					worst = std::max(worst, std::abs(y - grid.positions[static_cast<size_t>(z) * width + x].y));
				}
			}
		}
		return worst;
	}

	std::vector<Tile> SplitTiles(const Mesh& mesh, const std::vector<uint32_t>& indices, float tileMetres)
	{
		//Triangles of every square, in the order of the squares (z, then x)
		std::map<std::pair<int, int>, std::vector<uint32_t>> squares;
		for (size_t t = 0; t + 2 < indices.size(); t += 3) {
			std::pair<int, int> key{ 0, 0 };
			if (tileMetres > 0.0f) {
				const glm::vec3 centre = (mesh.positions[indices[t]] + mesh.positions[indices[t + 1]] + mesh.positions[indices[t + 2]]) / 3.0f;
				key = { static_cast<int>(std::floor(centre.z / tileMetres)), static_cast<int>(std::floor(centre.x / tileMetres)) };
			}
			std::vector<uint32_t>& triangles = squares[key];
			triangles.insert(triangles.end(), { indices[t], indices[t + 1], indices[t + 2] });
		}

		std::vector<Tile> tiles;
		tiles.reserve(squares.size());
		//local[v] is valid for the tile being built when owner[v] equals its number
		std::vector<uint32_t> owner(mesh.positions.size(), UINT32_MAX);
		std::vector<uint32_t> local(mesh.positions.size(), 0);
		const bool hasNormals = mesh.normals.size() == mesh.positions.size();
		const bool hasUvs = mesh.uvs.size() == mesh.positions.size();
		for (auto& [key, triangles] : squares) {
			Tile tile;
			tile.z = key.first;
			tile.x = key.second;
			const uint32_t number = static_cast<uint32_t>(tiles.size());
			tile.mesh.indices.reserve(triangles.size());
			for (uint32_t v : triangles) {
				if (owner[v] != number) {
					owner[v] = number;
					local[v] = static_cast<uint32_t>(tile.mesh.positions.size());
					tile.mesh.positions.push_back(mesh.positions[v]);
					if (hasNormals) {
						tile.mesh.normals.push_back(mesh.normals[v]);
					}
					if (hasUvs) {
						tile.mesh.uvs.push_back(mesh.uvs[v]);
					}
				}
				tile.mesh.indices.push_back(local[v]);
			}
			tiles.push_back(std::move(tile));
		}
		return tiles;
	}

	Mesh Quad(glm::vec2 min, glm::vec2 max, float y)
	{
		Mesh mesh;
		mesh.positions = { { min.x, y, min.y }, { max.x, y, min.y }, { min.x, y, max.y }, { max.x, y, max.y } };
		mesh.normals.assign(4, glm::vec3(0.0f, 1.0f, 0.0f));
		mesh.uvs = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } };
		mesh.indices = { 0, 2, 1, 1, 2, 3 };
		return mesh;
	}

	Mesh FromTriangles(const std::vector<glm::vec3>& vertices, glm::vec2 uvMin, glm::vec2 uvMax)
	{
		Mesh mesh;
		//Kept triangles, each counter-clockwise seen from above
		std::vector<glm::vec3> kept;
		kept.reserve(vertices.size());
		for (size_t t = 0; t + 2 < vertices.size(); t += 3) {
			glm::vec3 a = vertices[t], b = vertices[t + 1], c = vertices[t + 2];
			const glm::vec3 centre = (a + b + c) / 3.0f;
			if (centre.x < uvMin.x || centre.x > uvMax.x || centre.z < uvMin.y || centre.z > uvMax.y) {
				continue;
			}
			if (glm::cross(b - a, c - a).y < 0.0f) {
				std::swap(b, c);
			}
			kept.insert(kept.end(), { a, b, c });
		}
		if (kept.empty()) {
			return mesh;
		}

		std::vector<uint32_t> remap(kept.size());
		const size_t unique = meshopt_generateVertexRemap(remap.data(), nullptr, kept.size(), kept.data(), kept.size(), sizeof(glm::vec3));
		mesh.positions.resize(unique);
		meshopt_remapVertexBuffer(mesh.positions.data(), kept.data(), kept.size(), sizeof(glm::vec3), remap.data());
		mesh.indices = remap;

		mesh.normals.assign(unique, glm::vec3(0.0f));
		for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
			const glm::vec3& a = mesh.positions[mesh.indices[t]];
			const glm::vec3& b = mesh.positions[mesh.indices[t + 1]];
			const glm::vec3& c = mesh.positions[mesh.indices[t + 2]];
			//Length = twice the area
			const glm::vec3 face = glm::cross(b - a, c - a);
			for (int k = 0; k < 3; k++) {
				mesh.normals[mesh.indices[t + k]] += face;
			}
		}
		const glm::vec2 size = glm::max(uvMax - uvMin, glm::vec2(1e-6f));
		mesh.uvs.resize(unique);
		for (size_t v = 0; v < unique; v++) {
			const float length = glm::length(mesh.normals[v]);
			mesh.normals[v] = length > 0.0f ? mesh.normals[v] / length : glm::vec3(0.0f, 1.0f, 0.0f);
			mesh.uvs[v] = (glm::vec2(mesh.positions[v].x, mesh.positions[v].z) - uvMin) / size;
		}
		return mesh;
	}
}
