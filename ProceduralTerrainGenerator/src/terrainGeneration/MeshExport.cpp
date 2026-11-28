#include "MeshExport.h"

#include <algorithm>
#include <chrono>

#include "FileIO.h"
#include "MeshFile.h"
#include "TerrainMesh.h"

namespace meshexport {
	namespace {
		const char* generator = "Procedural Terrain Generator";

		bool WriteNodes(const std::filesystem::path& path, Format format, const std::vector<meshfile::Node>& nodes,
			const std::vector<meshfile::Material>& materials, std::string& error)
		{
			if (format == Format::GLB) {
				const std::vector<unsigned char> glb = meshfile::EncodeGlb(nodes, materials, generator, &error);
				if (glb.empty()) {
					if (error.empty()) {
						error = "Failed to encode GLB";
					}
					return false;
				}
				return WriteFileAtomic(path, glb.data(), glb.size(), error);
			}
			const std::string obj = meshfile::EncodeObj(nodes, generator);
			return WriteFileAtomic(path, obj.data(), obj.size(), error);
		}

		std::string FormatName(Format format) { return format == Format::GLB ? "glb" : "obj"; }
	}

	Result Write(const HeightField& map, const Settings& settings, const std::filesystem::path& folder)
	{
		const auto start = std::chrono::steady_clock::now();
		Result result;
		const int width = map.GetWidth(), height = map.GetHeight();
		const terrainmesh::Mesh grid = terrainmesh::BuildGrid(map, settings.metresPerUnit, settings.metresPerTexel);
		if (grid.positions.empty()) {
			result.error = "The map is smaller than 2 x 2 texels";
			return result;
		}

		std::vector<meshfile::Material> terrainMaterials(1);
		terrainMaterials[0].name = "terrain";
		if (settings.terrainTexture.empty()) {
			terrainMaterials[0].color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
		}
		else {
			terrainMaterials[0].texture = settings.terrainTexture;
		}

		std::vector<uint32_t> indices = grid.indices;
		for (int lod = 0; lod < std::max(settings.lodLevels, 1); lod++) {
			exportpack::MeshLevel level;
			level.lod = lod;
			if (lod > 0) {
				//From the previous level: much faster than from the full grid, about the same error (measured on 1024^2)
				indices = terrainmesh::Simplify(grid, indices, indices.size() / 3 / 4);
				level.errorMetres = terrainmesh::MaxHeightError(grid, indices, width, height, settings.metresPerTexel);
			}
			const std::vector<terrainmesh::Tile> tiles = terrainmesh::SplitTiles(grid, indices, settings.tileMetres);
			std::vector<meshfile::Node> nodes;
			nodes.reserve(tiles.size());
			for (const terrainmesh::Tile& tile : tiles) {
				nodes.push_back({ "terrain_" + std::to_string(tile.x) + "_" + std::to_string(tile.z) + "_LOD" + std::to_string(lod), { { &tile.mesh, 0 } } });
				level.vertices += tile.mesh.positions.size();
			}
			level.triangles = indices.size() / 3;
			level.tiles = static_cast<int>(tiles.size());
			level.path = settings.name + "_terrain_lod" + std::to_string(lod) + Extension(settings.format);
			if (!WriteNodes(folder / level.path, settings.format, nodes, terrainMaterials, result.error)) {
				result.error = level.path + ": " + result.error;
				return result;
			}
			result.files.push_back({ level.path, "terrain_lod" + std::to_string(lod), FormatName(settings.format), std::nullopt });
			result.levels.push_back(level);
		}

		if (settings.water) {
			const glm::vec2 mapMax(static_cast<float>(width - 1) * settings.metresPerTexel, static_cast<float>(height - 1) * settings.metresPerTexel);
			terrainmesh::Mesh sea, rivers;
			std::vector<meshfile::Node> nodes;
			if (settings.seaLevel) {
				sea = terrainmesh::Quad(glm::vec2(0.0f), mapMax, *settings.seaLevel * settings.metresPerUnit);
				nodes.push_back({ "sea", { { &sea, 0 } } });
			}
			if (!settings.riverWater.empty()) {
				rivers = terrainmesh::FromTriangles(settings.riverWater, glm::vec2(0.0f), mapMax);
				if (!rivers.indices.empty()) {
					nodes.push_back({ "rivers_lakes", { { &rivers, 0 } } });
				}
			}
			if (!nodes.empty()) {
				std::vector<meshfile::Material> waterMaterials(1);
				waterMaterials[0].name = "water";
				waterMaterials[0].color = glm::vec4(0.05f, 0.25f, 0.4f, 0.7f);
				waterMaterials[0].blend = true;
				waterMaterials[0].roughness = 0.05f;
				result.waterFile = settings.name + "_water" + Extension(settings.format);
				if (!WriteNodes(folder / result.waterFile, settings.format, nodes, waterMaterials, result.error)) {
					result.error = result.waterFile + ": " + result.error;
					result.waterFile.clear();
					return result;
				}
				for (const meshfile::Node& node : nodes) {
					result.waterNodes.push_back(node.name);
				}
				result.files.push_back({ result.waterFile, "water", FormatName(settings.format), std::nullopt });
			}
		}

		result.ok = true;
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	}
}
