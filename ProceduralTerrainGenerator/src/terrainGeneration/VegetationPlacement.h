#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "Biome.h"

//Where vegetation and rocks stand: a jittered grid in map texels, one grid per layer, each cell holding at most one model.
//Every cell lies wholly in one chunk (the cell sizes divide the chunk size), so chunks never share a cell: no seams and no
//doubles. Everything random comes from a hash of the cell, so a chunk always gets the same models. What grows where are the
//plants of the biomes (biome::Plant). No OpenGL here.
namespace vegetation
{
	enum class Layer { TREES, SHRUBS, ROCKS, COUNT };
	constexpr int layerCount = static_cast<int>(Layer::COUNT);
	//Side of a grid cell in map texels (1 texel = 1 m), per layer; each divides the chunk size
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

	//Maps of one chunk as read from the GPU: size x size texels, row-major, texel t is map texel firstTexel + t * step
	//(a coarser chunk samples every step-th map texel)
	struct ChunkMaps {
		int size = 0;
		glm::ivec2 firstTexel = glm::ivec2(0);
		int step = 1;
		const float* height = nullptr;
		const glm::vec4* surface = nullptr;		//xy = height gradient per world unit (GpuSurfacePass)
		const int* ids = nullptr;				//biome ids
	};

	struct PlacementSettings {
		glm::ivec2 chunkTexel = glm::ivec2(0);	//first map texel of the chunk
		int chunkTexels = 128;					//side of the chunk in texels
		glm::vec2 mapOrigin = glm::vec2(0.0f);	//world XZ of map texel (0, 0)
		float worldUnitsPerTexel = 4.0f;
		float heightScale = 1.0f;				//world units of height 1, for slopes
		float waterHeight = 0.0f;				//nothing stands at or below it
		float density = 1.0f;					//multiplies every probability
		uint32_t seed = 0;
		unsigned layerMask = (1u << layerCount) - 1u;	//bit l = layer l is placed
	};

	//Places the plants of the biomes on a chunk. In each cell of a layer at most one plant stands: of the plants of the cell's biome
	//in this layer that can grow there (its elevation range and slope), each takes its density (times the settings density and
	//its clustering) of [0, 1) in turn, and a hash of the cell picks one or none
	//@param plants - plants of every biome id (plants[id] = the plants of table entry id); ids beyond it have none
	//@param models - every model a plant may name, with its layer and number of variants
	//@param out - instances of the chunk are appended
	void PlaceChunk(const ChunkMaps& maps, const std::vector<std::vector<biome::Plant>>& plants, const std::vector<ModelInfo>& models,
		const PlacementSettings& settings, std::vector<Instance>& out);

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
