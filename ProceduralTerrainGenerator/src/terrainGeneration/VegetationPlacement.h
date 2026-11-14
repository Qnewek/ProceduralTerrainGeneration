#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "glm/glm.hpp"

//Where vegetation and rocks stand: a jittered grid in map texels, one grid per layer, each cell holding at most one model.
//Every cell lies wholly in one chunk (the cell sizes divide the chunk size), so chunks never share a cell: no seams and no
//doubles. Everything random comes from a hash of the cell, so a chunk always gets the same models. No OpenGL here.
namespace vegetation
{
	enum class Layer { TREES, SHRUBS, ROCKS, COUNT };
	constexpr int layerCount = static_cast<int>(Layer::COUNT);
	//Side of a grid cell in map texels (1 texel = 1 m), per layer; each divides the chunk size
	constexpr int cellTexels[layerCount] = { 8, 4, 8 };
	//World units of one metre of a model (1 texel = 1 m = TerrainView::worldUnitsPerTexel)
	constexpr float unitsPerMetre = 4.0f;

	//One kind of model in a biome
	struct Species {
		std::string model;			//name in vegetation_models.json
		Layer layer = Layer::TREES;
		float probability = 0.0f;	//chance that a cell of the layer holds this species, before clustering
		float minScale = 1.0f, maxScale = 1.0f;	//size relative to the model
		float maxSlope = 35.0f;		//degrees
	};
	//Species of the biome with this name (biome table entries are matched by name)
	struct BiomeRules {
		std::string biome;
		std::vector<Species> species;
	};
	//Rules for the default biome table (Biome.cpp)
	std::vector<BiomeRules> DefaultRules();

	//A model known to the placement: its name and the number of its variants (0 = not loaded, never placed)
	struct ModelInfo {
		std::string name;
		int variants = 0;
	};

	struct Instance {
		glm::vec3 position = glm::vec3(0.0f);	//x, z in world units (before modelScale); y = terrain height as in the height map
		float scale = 1.0f;		//world units per metre of the model
		float yaw = 0.0f;		//radians
		int model = 0;
		int variant = 0;
		Layer layer = Layer::TREES;
	};

	//Maps of one chunk as read from the GPU: size x size texels, row-major, texel t is map texel firstTexel + t
	struct ChunkMaps {
		int size = 0;
		glm::ivec2 firstTexel = glm::ivec2(0);
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
	};

	//Rules of every biome id: rules[id] for the biome table entry id, nullptr = no vegetation there
	//@param models - every model a species may name, with its number of variants
	//@param out - instances of the chunk are appended
	void PlaceChunk(const ChunkMaps& maps, const std::vector<const BiomeRules*>& rules, const std::vector<ModelInfo>& models,
		const PlacementSettings& settings, std::vector<Instance>& out);

	//Clustering of trees and shrubs (forests and clearings): a factor around 1 from a smooth noise of the map position
	float ClusterFactor(glm::vec2 texel, uint32_t seed);
}
