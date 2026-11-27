#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "Biome.h"
#include "ExportManifest.h"
#include "Grid.h"
#include "Hydrology.h"
#include "VegetationPlacement.h"

//Vegetation of an export pack: the plants of the finite map placed exactly as in the infinite world (the same cells, hashes and
//settings, vegetation::PlaceChunk), written as <name>_vegetation.json, and optionally the models they use as glTF files with their
//variants as nodes. No OpenGL here: works on maps read beforehand, so it can run on a background thread.
namespace vegexport {
	//Chunk the map is placed in, in metres of the placement grid; any multiple of every vegetation::cellTexels gives the same plants
	//(maps of pixels of p >= 2 m are placed in chunks of chunkTexels * floor(p))
	inline constexpr int chunkTexels = 128;
	//Side in metres of the chunks PlaceMap places a map of pixels of p metres in: chunkTexels * floor(p) from 1 m, below it 8 * floor(16 p)
	//(at least 8): always a multiple of every cell, so the plants do not depend on it, and about 128 pixels a side
	int ChunkMetres(float pixel);
	//PlaceMap places nothing on smaller pixels: a chunk of the smallest side (8 m) would be a window of more than 800 pixels a side per thread
	inline constexpr float minPlantPixelMetres = 0.01f;
	//Plants a map of the "Imported map" mode may have (stage D4): above it only trees, and with more trees than this none
	inline constexpr size_t maxMapInstances = 2'000'000;

	//A model the plants may name
	struct Model {
		std::string name, kind, asset;
		int variants = 0;					//as loaded by the app; 0 = not loaded, never placed
		std::filesystem::path source;		//its glTF file
		std::vector<uint32_t> lodTargets;	//triangles of its levels of detail, as the app builds them; empty = the source mesh only
		std::filesystem::path cache;		//the app's cache of those levels (vegetation::SaveCache); empty or stale = built again
		std::vector<float> sinkPerSlope;	//per variant, as loaded by the app (vegetation::Variant::sinkPerSlope)
	};

	//A plant in metres, axes of the pack: position = where the base of the model stands on the terrain
	struct Placed {
		glm::vec3 position = glm::vec3(0.0f);
		float yaw = 0.0f;		//radians around +Y (right-handed, as the app turns the models)
		float scale = 1.0f;		//metres per metre of the model
		int model = 0;
		int variant = 0;
		vegetation::Layer layer = vegetation::Layer::TREES;
	};

	//Distances in metres at which the app switches the levels of detail of a layer: LOD 0 below lod1, LOD 1 below lod2, the last mesh
	//level up to max (the app's impostors are not exported)
	struct LodRange {
		float lod1 = 0.0f, lod2 = 0.0f, max = 0.0f;
	};
	using LodRanges = std::array<LodRange, vegetation::layerCount>;
	//The app's defaults (VegetationLayer), in vegetation::Layer order
	inline constexpr LodRanges defaultLodRanges = { { { 40.0f, 100.0f, 3000.0f }, { 15.0f, 40.0f, 120.0f }, { 20.0f, 60.0f, 200.0f } } };

	struct Input {
		std::vector<std::vector<biome::Plant>> plants;	//plants of every biome id
		std::vector<Model> models;
		//Seed, density, height scale and water height as the app places with; the chunk, origin and texel size are set by PlaceMap
		vegetation::PlacementSettings settings;
		std::shared_ptr<const hydrology::Network> rivers;	//nothing stands in their water; nullptr = none
		float metresPerUnit = 1.0f;		//metres of height value 1
		float metresPerTexel = 1.0f;	//size of a map pixel (generated maps: 1)
		//Position of map texel (0, 0) on the placement grid in whole metres (TerrainGen: world map position of a partial map moved over the world)
		glm::ivec2 mapTexel = glm::ivec2(0);
		uint32_t layerMask = (1u << vegetation::layerCount) - 1u;	//bit i = vegetation::Layer i is placed
		size_t limit = 0;				//PlaceMapLimited: plants at most (maxMapInstances); 0 = no limit
		//Set (from another thread) = stop as soon as possible; what was placed until then is incomplete. nullptr = never
		std::shared_ptr<const std::atomic<bool>> cancel;
		//Write: plants put by hand (model = index into models), written after the placed ones; never limited, also when kept is NONE
		std::vector<Placed> extra;
		LodRanges lodRanges = defaultLodRanges;	//written to the instance file for the engine importers
	};

	//Places the plants of a finite map in the chunks of the metre grid it lies on; plants beyond its edge texels are left out. Positions are
	//relative to its texel (0, 0). Chunk rows are placed on worker threads; the result is the same in any case. With Input::limit > 0 it stops
	//once it has more than that many plants (the result then has more than limit, but not all of them)
	//@param surface - surface map of the height map (GpuSurfacePass: xy = gradient per world unit), as the chunks have it; empty = slopes
	//from the heights (metres over metres, Input::metresPerUnit), as for the maps of the real world
	//@param ids - biome ids
	std::vector<Placed> PlaceMap(const HeightField& height, const std::vector<glm::vec4>& surface, const BiomeMap& ids, const Input& input);

	//What PlaceMapLimited kept of the plants
	enum class Kept { ALL, TREES_ONLY, NONE };
	struct Limited {
		std::vector<Placed> placed;
		Kept kept = Kept::ALL;
	};
	//PlaceMap within Input::limit: the trees first, none at all when they are more than the limit; then the other layers of the mask, only
	//the trees when all together are more. Limit 0 = PlaceMap
	Limited PlaceMapLimited(const HeightField& height, const std::vector<glm::vec4>& surface, const BiomeMap& ids, const Input& input);

	//The plants of PlaceMap as the app draws them (VegetationLayer): XZ in world units before modelScale, Y = height value, scale in world units per metre
	//@param texelZeroWorld - world XZ where the centre of map texel (0, 0) is drawn
	//@param metresPerUnit - as Input::metresPerUnit
	//@param metresPerTexel - as Input::metresPerTexel: a texel is drawn TerrainView::worldUnitsPerTexel wide
	std::vector<vegetation::Instance> ToAppInstances(const std::vector<Placed>& placed, glm::vec2 texelZeroWorld, float metresPerUnit,
		float metresPerTexel = 1.0f);

	//The variants of a model as written to its file
	struct ModelFile {
		std::string path;					//relative to the pack; empty = not written
		std::vector<std::vector<std::string>> nodes;	//nodes of every variant (in the order of the variant ids), nearest level first
		std::vector<uint32_t> lodTriangles;	//triangles of every level of the first variant
		std::string error;					//why it was not written
	};

	//Writes a model as binary glTF: a node <name>_<variant>_LOD<n> per variant and level of detail (as the app builds and draws them:
	//vegetation::BuildLods, base at the origin, metres), its materials with the textures copied next to it (an albedo with a separate
	//alpha map is merged into one RGBA PNG)
	//@param folder - folder of the pack; the model goes to vegetation/<asset>.glb, its textures to vegetation/textures/
	ModelFile WriteModel(const Model& model, const std::filesystem::path& folder);

	//<name>_vegetation.json: the models (with their files and variant nodes, if written), the LOD distances of every layer and the instances as rows
	std::string InstancesJson(const std::vector<Placed>& placed, const std::vector<Model>& models, const std::vector<ModelFile>& files,
		const LodRanges& lodRanges = defaultLodRanges);

	struct Result {
		bool ok = false;
		std::string error;
		std::vector<exportpack::File> files;
		size_t instances = 0;
		std::vector<std::string> modelErrors;	//models that could not be written ("name: why")
		Kept kept = Kept::ALL;	//what the limit left (Input::limit)
		double ms = 0.0;
	};

	//Places the plants (PlaceMapLimited) and writes <name>_vegetation.json and, if writeModels, the models used by at least one plant
	Result Write(const HeightField& height, const std::vector<glm::vec4>& surface, const BiomeMap& ids, const Input& input, const std::string& name,
		bool writeModels, const std::filesystem::path& folder);
}
