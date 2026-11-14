#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <GL/glew.h>
#include "glm/glm.hpp"

#include "ChunkWorld.h"
#include "VegetationPlacement.h"

class Camera;
class LightSource;
class VegetationModels;
struct TerrainRenderSettings;

//Vegetation and rocks standing on the infinite world: every chunk of full detail near the camera gets its instances once
//(maps read back from the GPU, vegetation::PlaceChunk), and every frame the instances in range and in the frustum are sorted
//by model, variant and level of detail into one buffer and drawn with instancing. Only for the thread owning the GL context.
class VegetationLayer
{
public:
	VegetationLayer() = default;
	~VegetationLayer();
	VegetationLayer(const VegetationLayer&) = delete;
	VegetationLayer& operator=(const VegetationLayer&) = delete;

	//What placement needs of the world, besides the chunks
	struct World {
		std::vector<std::string> biomeNames;	//biome table, id = index
		float waterHeight = 0.0f;
		float heightScale = 1.0f;
		uint32_t seed = 0;
	};

	//Places instances on new and regenerated chunks (a few per frame: reading the maps waits for the GPU) and forgets chunks out of range
	//@param models - nullptr = no vegetation
	void Update(const ChunkWorld& chunks, const VegetationModels* models, const World& world);
	void Draw(Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, VegetationModels* models);
	//Settings and statistics, in the Vegetation tab
	void ImGuiDraw();

	//Chunks whose maps are read back per frame at most
	static constexpr int maxPlacementsPerFrame = 4;

private:
	struct ChunkInstances {
		uint64_t generation = 0;
		std::vector<vegetation::Instance> instances;
	};
	//Per layer of vegetation::Layer, in metres
	struct LayerRange {
		float lod1 = 0.0f, lod2 = 0.0f, max = 0.0f;
	};

	std::unordered_map<uint64_t, ChunkInstances> placed;
	//Settings the placed instances were made with; a change places everything again
	std::vector<std::string> placedBiomes;
	float placedWater = 0.0f, placedDensity = 0.0f;
	uint32_t placedSeed = 0;
	int placedReadyModels = -1;

	bool enabled = true;
	float density = 1.0f;
	LayerRange ranges[vegetation::layerCount] = { { 40.0f, 120.0f, 450.0f }, { 15.0f, 40.0f, 120.0f }, { 20.0f, 60.0f, 200.0f } };

	//Instance buffer (VegetationModels::instanceBinding), regrown when too small
	GLuint instanceBuffer = 0;
	size_t instanceCapacity = 0;

	//Last frame
	int drawnInstances = 0;
	uint64_t drawnTriangles = 0;
	int drawnPerLod[3] = {};
	double placementMs = 0.0;
};
