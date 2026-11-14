#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <GL/glew.h>
#include "glm/glm.hpp"

#include "ChunkWorld.h"
#include "GenerationJob.h"
#include "RangeAllocator.h"
#include "VegetationPlacement.h"

class Camera;
class LightSource;
class Shader;
class VegetationModels;
struct TerrainRenderSettings;

//Vegetation and rocks standing on the infinite world. Every ground chunk (leaf of the chunk quadtree in range) gets its instances
//once: maps read back from the GPU, vegetation::PlaceChunk; chunks of full detail get all layers, coarser ones (far away) only trees,
//which are impostors there. The instances of a chunk stay on the GPU in one buffer, sorted by blocks of 128 m in Morton order, so a part
//of a coarser chunk can stand in for finer chunks not placed yet. Every frame a compute shader (cull_compute.shader) culls the instances
//of the chunks in view by frustum and distance, picks their level of detail and sorts them into groups (model, variant, level),
//and every model is drawn with indirect multi-draws. Only for the thread owning the GL context.
class VegetationLayer
{
public:
	VegetationLayer();
	~VegetationLayer();
	VegetationLayer(const VegetationLayer&) = delete;
	VegetationLayer& operator=(const VegetationLayer&) = delete;

	//What placement needs of the world, besides the chunks
	struct World {
		const std::vector<biome::Biome>* biomes = nullptr;	//biome table, id = index; nullptr while biomes are off (no vegetation)
		float waterHeight = 0.0f;
		float heightScale = 1.0f;
		uint32_t seed = 0;
	};

	//Places instances on new and regenerated ground chunks and forgets the others. The maps of a few chunks per frame are read back
	//(it waits for the GPU); placing runs on worker threads and the results are installed by later Updates
	//@param models - nullptr = no vegetation
	void Update(const ChunkWorld& chunks, const VegetationModels* models, const World& world);
	void Draw(Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, VegetationModels* models);
	//Settings and statistics, in the Vegetation tab
	void ImGuiDraw();
	bool ReloadShader();

	//Multiplies the density of every plant (the Density slider); a change places everything again
	void SetDensity(float value) { density = value; }
	//Distance in metres up to which trees are placed and drawn (the Range of trees)
	void SetTreeRange(float metres) { ranges[static_cast<int>(vegetation::Layer::TREES)].max = metres; }
	//Off = LOD 2 instead of impostors, for comparing them
	void SetImpostors(bool on) { impostors = on; }
	//Instances placed on all chunks
	size_t PlacedCount() const;
	//Ground chunks placed with older settings or not at all (also those being placed), at the last Update
	int UnplacedCount() const { return unplaced; }
	//Drawn by the frame before the last Draw (counts come back from the GPU a frame late)
	int DrawnInstances() const { return drawnInstances; }
	uint64_t DrawnTriangles() const { return drawnTriangles; }
	//@param lod - 0 to 2, or impostorLod
	int DrawnInLod(int lod) const { return drawnPerLod[lod]; }
	static constexpr int impostorLod = 3;
	double LastPlacementMs() const { return placementMs; }

	//Chunks whose maps are read back per frame at most, and chunks being placed on worker threads at most
	static constexpr int maxPlacementsPerFrame = 4;
	static constexpr int maxPlacingJobs = 8;

private:
	struct ChunkInstances {
		uint64_t generation = 0;
		//Placed with older settings: drawn until placed again, so a change does not make the vegetation blink out
		bool stale = false;
		int level = 0;
		glm::ivec2 coord = glm::ivec2(0);
		//Range in the placed buffer, and the start of every block of chunkSize texels in it, by Morton index ((2^level)^2 + 1 entries)
		uint32_t offset = 0, count = 0;
		std::vector<uint32_t> blockStarts;
	};
	//Instances [first, first + count) of the placed buffer to look at this frame, with the box of the area they stand on
	struct DrawRange {
		uint32_t first = 0, count = 0;
		glm::vec2 areaMin = glm::vec2(0.0f), areaMax = glm::vec2(0.0f);	//world XZ
	};
	//Per layer of vegetation::Layer, in metres; impostors only for models that have them (trees), others keep LOD 2 to the range
	struct LayerRange {
		float lod1 = 0.0f, lod2 = 0.0f, impostor = 0.0f, max = 0.0f;
	};
	//A GL buffer grown as needed (contents are not kept)
	struct Buffer {
		GLuint id = 0;
		size_t bytes = 0;
		void Reserve(size_t needed);
	};

	//Instances of a chunk made by a worker, sorted by blocks
	struct PlacementResult {
		std::vector<vegetation::Instance> instances;
		std::vector<uint32_t> blockStarts;
		double ms = 0.0;
	};
	//A chunk being placed on a worker thread
	struct Placing {
		uint64_t key = 0, generation = 0, settingsVersion = 0;
		int level = 0;
		glm::ivec2 coord = glm::ivec2(0);
		std::unique_ptr<GenerationJob<PlacementResult>> job;
	};

	//Reads the maps of a ground chunk from the GPU and starts placing it on a worker thread
	void StartPlacement(const ChunkWorld& chunks, const ChunkWorld::GroundChunk& ground, const World& world);
	//Moves the instances of a finished placement to the GPU
	void Install(const Placing& placing, PlacementResult& result);
	//Gives the range of a chunk back
	void Release(ChunkInstances& chunk);
	//Range of the placed buffer for count instances, growing the buffer (and copying it) when it is full
	uint32_t Allocate(uint32_t count);
	//Tables of the culling (groups, commands, bounds) for the models ready now
	void BuildTables(const VegetationModels& models);
	//Instances to draw: every ground chunk's own, or the part of a placed coarser chunk over it, or its placed finer chunks
	void CollectRanges(const ChunkWorld& chunks);

	std::unordered_map<uint64_t, ChunkInstances> placed;
	std::vector<Placing> placing;
	//Changes with every change of the placement settings; a placement started with an older one is installed as stale
	uint64_t settingsVersion = 0;
	std::vector<ChunkWorld::GroundChunk> ground;
	std::vector<DrawRange> drawRanges;
	//Settings the placed instances were made with; a change places everything again
	std::vector<std::vector<biome::Plant>> placedPlants;
	float placedWater = 0.0f, placedDensity = 0.0f;
	uint32_t placedSeed = 0;
	int placedReadyModels = -1;

	bool enabled = true;
	bool impostors = true;
	float density = 1.0f;
	//Multiplies the distances of the levels of detail and impostors of every layer (not the ranges)
	float detailScale = 1.0f;
	LayerRange ranges[vegetation::layerCount] = { { 40.0f, 100.0f, 150.0f, 3000.0f }, { 15.0f, 40.0f, 120.0f, 120.0f }, { 20.0f, 60.0f, 200.0f, 200.0f } };

	//All placed instances (VegetationModels::GpuInstance), ranges handed out per chunk
	GLuint placedBuffer = 0;
	RangeAllocator allocator;
	//Per frame: ranges, classification and the culled instances in groups
	Buffer rangesBuffer, classifiedBuffer, drawnBuffer;
	//Tables: per group counts, offsets and the counts of the last frame for the statistics; bounds of variants, models; commands
	GLuint countsBuffer = 0, offsetsBuffer = 0, statsBuffer = 0, variantsBuffer = 0, modelsBuffer = 0;
	GLuint meshCommandBuffer = 0, meshGroupBuffer = 0, impostorCommandBuffer = 0, impostorGroupBuffer = 0;
	std::unique_ptr<Shader> cullShader;
	int tablesReadyModels = -1;
	//The models as placement knows them (name, variants of ready ones, layer), made with the tables
	std::vector<vegetation::ModelInfo> modelInfos;
	int groupCount = 0, meshCommandCount = 0, impostorCommandCount = 0;
	std::vector<int> meshCommandFirst, impostorCommandFirst;	//per model, -1 = none
	std::vector<uint64_t> groupTriangles;
	std::vector<int> groupLod;
	bool statsPending = false;

	//Last frame
	int drawnInstances = 0;
	uint64_t drawnTriangles = 0;
	int drawnPerLod[4] = {};	//LOD 0 - 2 and impostors
	double placementMs = 0.0;
	int unplaced = 0;
};
