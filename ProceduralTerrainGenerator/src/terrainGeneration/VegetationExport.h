#pragma once

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
	//Chunk the map is placed in; any multiple of every vegetation::cellTexels gives the same plants
	inline constexpr int chunkTexels = 128;

	//A model the plants may name
	struct Model {
		std::string name, kind, asset;
		int variants = 0;					//as loaded by the app; 0 = not loaded, never placed
		std::filesystem::path source;		//its glTF file
		std::vector<uint32_t> lodTargets;	//triangles of its levels of detail, as the app builds them; empty = the source mesh only
		std::filesystem::path cache;		//the app's cache of those levels (vegetation::SaveCache); empty or stale = built again
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

	struct Input {
		std::vector<std::vector<biome::Plant>> plants;	//plants of every biome id
		std::vector<Model> models;
		//Seed, density, height scale and water height as the app places with; the chunk, origin and texel size are set by PlaceMap
		vegetation::PlacementSettings settings;
		std::shared_ptr<const hydrology::Network> rivers;	//nothing stands in their water; nullptr = none
		float metresPerUnit = 1.0f;		//metres of height value 1
		glm::ivec2 mapTexel = glm::ivec2(0);	//world map position of map texel (0, 0) (a partial map moved over the world)
		uint32_t layerMask = (1u << vegetation::layerCount) - 1u;	//bit i = vegetation::Layer i is placed
	};

	//Places the plants of a finite map in the chunks of chunkTexels of the world grid it lies on; plants beyond its edge texels are left
	//out. Positions are relative to its texel (0, 0)
	//@param surface - surface map of the height map (GpuSurfacePass: xy = gradient per world unit), as the chunks have it
	//@param ids - biome ids
	std::vector<Placed> PlaceMap(const HeightField& height, const std::vector<glm::vec4>& surface, const BiomeMap& ids, const Input& input);
	//The plants of PlaceMap as the app draws them (VegetationLayer): XZ in world units before modelScale, Y = height value, scale in world units per metre
	//@param texelZeroWorld - world XZ where the centre of map texel (0, 0) is drawn
	//@param metresPerUnit - as Input::metresPerUnit
	std::vector<vegetation::Instance> ToAppInstances(const std::vector<Placed>& placed, glm::vec2 texelZeroWorld, float metresPerUnit);

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

	//<name>_vegetation.json: the models (with their files and variant nodes, if written) and the instances as rows
	std::string InstancesJson(const std::vector<Placed>& placed, const std::vector<Model>& models, const std::vector<ModelFile>& files);

	struct Result {
		bool ok = false;
		std::string error;
		std::vector<exportpack::File> files;
		size_t instances = 0;
		std::vector<std::string> modelErrors;	//models that could not be written ("name: why")
		double ms = 0.0;
	};

	//Places the plants and writes <name>_vegetation.json and, if writeModels, the models used by at least one plant
	Result Write(const HeightField& height, const std::vector<glm::vec4>& surface, const BiomeMap& ids, const Input& input, const std::string& name,
		bool writeModels, const std::filesystem::path& folder);
}
