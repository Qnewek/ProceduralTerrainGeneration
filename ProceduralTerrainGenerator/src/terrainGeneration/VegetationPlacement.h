#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "Biome.h"
#include "Hydrology.h"

//Where vegetation and rocks stand: a jittered grid in metres (the placement grid; a TerrainGen map texel is 1 m of it), one grid
//per layer, each cell holding at most one model. Every cell lies wholly in one chunk (the cell sizes divide the chunk size), so
//chunks never share a cell: no seams and no doubles. Everything random comes from a hash of the cell, so a chunk always gets the
//same models. What grows where are the plants of the biomes (biome::Plant). No OpenGL here.
namespace vegetation
{
	enum class Layer { TREES, SHRUBS, ROCKS, COUNT };
	constexpr int layerCount = static_cast<int>(Layer::COUNT);
	//Side of a grid cell in metres of the placement grid (TerrainGen: map texels), per layer; each divides the chunk size
	constexpr int cellTexels[layerCount] = { 8, 4, 8 };
	//World units of one metre of a model (1 texel = 1 m = TerrainView::worldUnitsPerTexel)
	constexpr float unitsPerMetre = 4.0f;
	//Names of the layers for the UI, in the order of Layer
	constexpr const char* layerNames[layerCount] = { "Trees", "Shrubs", "Rocks" };

	//Layer of a model of this kind (the kind in vegetation_models.json: tree, shrub, rock); other kinds are shrubs
	Layer LayerOfKind(const std::string& kind);

	//A model known to the placement: its name, layer and number of variants (0 = not loaded, never placed)
	struct ModelInfo {
		std::string name;
		int variants = 0;
		Layer layer = Layer::TREES;
	};

	struct Instance {
		glm::vec3 position = glm::vec3(0.0f);	//x, z in world units (before modelScale); y = terrain height as in the height map
		float scale = 1.0f;		//world units per metre of the model
		float yaw = 0.0f;		//radians
		int model = 0;
		int variant = 0;
		Layer layer = Layer::TREES;
	};

	//Maps of one chunk: size x size texels, row-major, texel t lies at firstTexel + t * step of the placement grid (metres). TerrainGen:
	//step 1, or 2^L for a coarser chunk sampling every 2^L-th map texel; a map of the real world: its pixel size
	struct ChunkMaps {
		int size = 0;
		glm::vec2 firstTexel = glm::vec2(0.0f);
		float step = 1.0f;
		const float* height = nullptr;
		const glm::vec4* surface = nullptr;		//xy = height gradient per world unit (GpuSurfacePass)
		const int* ids = nullptr;				//biome ids
	};

	struct PlacementSettings {
		glm::ivec2 chunkTexel = glm::ivec2(0);	//first metre of the chunk on the placement grid (TerrainGen: map texel)
		int chunkTexels = 128;					//side of the chunk in metres of the grid
		glm::vec2 mapOrigin = glm::vec2(0.0f);	//world XZ of grid position (0, 0)
		float worldUnitsPerTexel = 4.0f;		//world units per metre of the grid
		float heightScale = 1.0f;				//world units of height 1, for slopes (the surface map gradient is per world unit)
		float waterHeight = 0.0f;				//nothing stands at or below it
		const hydrology::Network* rivers = nullptr;	//nothing stands under the water of its rivers and lakes; nullptr = none
		float density = 1.0f;					//multiplies every probability
		uint32_t seed = 0;
		unsigned layerMask = (1u << layerCount) - 1u;	//bit l = layer l is placed
		//Height the elevation ranges of the plants see: biome::Elevation(h * elevationScale + elevationOffset, elevationWater), with
		//waterHeight when elevationWater is NaN (the defaults: the heights of the maps as they are)
		float elevationScale = 1.0f, elevationOffset = 0.0f;
		float elevationWater = std::numeric_limits<float>::quiet_NaN();
		//Where the river network lies: network position = (grid position - riverOrigin) * riverPositionScale, and its levels times
		//riverHeightScale are heights of the maps
		glm::vec2 riverOrigin = glm::vec2(0.0f);
		//Metres of the grid taken away (as integers) before a position becomes a float: maps.firstTexel, riverOrigin and the positions
		//placed are relative to it, so they keep centimetres far from the grid origin (riverOrigin then too: grid origin of the network
		//minus gridOrigin). Cells, their hashes and the clustering stay on the grid
		glm::ivec2 gridOrigin = glm::ivec2(0);
		float riverPositionScale = 1.0f, riverHeightScale = 1.0f;
	};

	//A plant of a biome with its model resolved
	struct ResolvedPlant {
		const biome::Plant* plant = nullptr;
		int model = 0;
	};
	//Plants of every biome id, per layer, with their models; plants of unknown models are left out, while models still loading (no
	//variants) keep their share of the cells, so the others do not move once they load
	using PreparedPlants = std::vector<std::array<std::vector<ResolvedPlant>, layerCount>>;
	//@param plants - must outlive the result (it points into it)
	PreparedPlants PreparePlants(const std::vector<std::vector<biome::Plant>>& plants, const std::vector<ModelInfo>& models);
	//Orders instances by squares of the given side over XZ, rows (z) first, then x; stable inside a square. A set given in this order
	//is culled square by square (VegetationLayer::SetExtraInstances), so sorting it off the GL thread saves that frame the work
	void SortIntoSquares(std::vector<Instance>& instances, float square);

	//Places the plants of the biomes on a chunk. In each cell of a layer at most one plant stands: of the plants of the cell's biome
	//in this layer that can grow there (its elevation range and slope), each takes its density (times the settings density and
	//its clustering) of [0, 1) in turn, and a hash of the cell picks one or none
	//@param plants - plants of every biome id (plants[id] = the plants of table entry id); ids beyond it have none
	//@param models - every model a plant may name, with its layer and number of variants
	//@param out - instances of the chunk are appended
	void PlaceChunk(const ChunkMaps& maps, const std::vector<std::vector<biome::Plant>>& plants, const std::vector<ModelInfo>& models,
		const PlacementSettings& settings, std::vector<Instance>& out);
	//The same with the plants prepared once for many chunks
	void PlaceChunk(const ChunkMaps& maps, const PreparedPlants& plants, const std::vector<ModelInfo>& models, const PlacementSettings& settings,
		std::vector<Instance>& out);

	//Z-order (Morton) index of a block in a square of blocks: every aligned power-of-two square of blocks is a contiguous range
	inline uint32_t Morton(uint32_t x, uint32_t y)
	{
		auto spread = [](uint32_t v) {
			v &= 0xFFFFu;
			v = (v | (v << 8)) & 0x00FF00FFu;
			v = (v | (v << 4)) & 0x0F0F0F0Fu;
			v = (v | (v << 2)) & 0x33333333u;
			v = (v | (v << 1)) & 0x55555555u;
			return v;
		};
		return spread(x) | (spread(y) << 1);
	}
	//Sorts the instances of a chunk by the Morton index of the block (blockTexels square) they stand in, so the instances of any
	//aligned power-of-two square of blocks are contiguous
	//@param chunkTexel - first map texel of the chunk; it spans blocksPerSide blocks (a power of two) per side
	//@return start of every block's instances by Morton index, blocksPerSide^2 + 1 entries (the last = instance count)
	std::vector<uint32_t> SortByBlock(std::vector<Instance>& instances, glm::vec2 mapOrigin, float worldUnitsPerTexel, glm::ivec2 chunkTexel,
		int blockTexels, int blocksPerSide);

	//Clustering of trees and shrubs (forests and clearings): a factor around 1 from a smooth noise of the map position
	float ClusterFactor(glm::vec2 texel, uint32_t seed);
	//Clustering factor of a plant: 1 + (ClusterFactor - 1) * clustering, so 0 spreads it evenly
	inline float PlantCluster(float clusterFactor, float clustering) { return 1.0f + (clusterFactor - 1.0f) * clustering; }
}
