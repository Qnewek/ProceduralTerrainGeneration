#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "Biome.h"
#include "EngineExport.h"
#include "ExportManifest.h"
#include "GenerationJob.h"
#include "HeightmapExport.h"
#include "MaterialExport.h"
#include "MeshExport.h"
#include "VegetationExport.h"

class TextureClass;

//ImGui "Saving" section writing an export pack: a folder <exports>/<name>/ with the height maps, optionally the material and
//biome maps, the meshes and the vegetation, and <name>.json describing them (exportpack::Manifest). Maps are read and written at
//once on the main thread; meshes and vegetation are built on a background thread, and the manifest is written when they are done.
class ExportPanel
{
public:
	//One height map to write, each one goes to its own file
	struct Target {
		int width, height;
		const char* suffix;		//added to the file name and the content, e.g. "_eroded_pipes"; "" for the main map
		std::function<HeightField()> read;	//called only when exporting, because a map may have to come from the GPU
	};

	//Shares of the surface materials (splat map) of the main map
	struct MaterialSource {
		std::vector<std::string> names;		//every material in the order of their ids
		std::function<splatmap::Shares()> read;	//called only when exporting; one channel per name
		//Textures of every material (id order) for a map of the given pixel size; empty function = none ("Material textures" disabled)
		std::function<std::vector<engineexport::MaterialTextures>(float metresPerTexel)> textures;
	};

	//Biome ids and colors of the main map
	struct BiomeSource {
		std::vector<exportpack::Biome> table;	//id, name and color of every biome; the panel measures the coverage
		std::function<BiomeMap()> readIds;
		std::function<std::vector<float>()> readColors;	//RGBA floats per texel
	};

	//Plants of the main map, placed as in the infinite world; every function is called only when exporting
	struct VegetationSource {
		std::function<vegexport::Input()> input;
		std::function<std::vector<glm::vec4>()> readSurface;	//surface map of the main map
		std::function<BiomeMap()> readIds;
	};

	//What the subsystem exports
	struct Pack {
		const char* mode = "";				//generation mode, stored in the manifest
		heightmap::ValueRange range{ 0.0f, 1.0f };	//values mapped to the 16-bit range
		float metresPerUnit = 1.0f;			//metres of height value 1 (display height scale / world units per metre)
		float metresPerTexel = 1.0f;		//metres between neighbouring map samples: 1 for generated maps, the pixel size of imported ones
		std::optional<float> seaLevel;		//height value of the sea surface, if the mode has a sea
		glm::ivec2 worldOrigin = glm::ivec2(0);	//world map position of texel (0, 0) of the main map
		std::function<std::string()> generation;	//generation parameters as preset JSON, called only when exporting
		std::vector<Target> heightmaps;		//the first one is the main map
		std::optional<MaterialSource> materials;	//none = the map has no material maps now
		std::optional<BiomeSource> biomes;			//none = the map has no biomes now
		const char* layersMissing = nullptr;		//why the material and biome maps are missing, shown instead of their checkboxes
		//Water surface of rivers and lakes as triangles (3 vertices each, metres, axes of terrainmesh); empty function = none
		std::function<std::vector<glm::vec3>()> riverWater;
		const char* riverWaterMissing = nullptr;	//why rivers and lakes are left out of the water file now (still being computed)
		std::optional<VegetationSource> vegetation;	//none = no vegetation now
		const char* vegetationMissing = nullptr;	//why, shown on the disabled checkbox
	};

	//@param _folder - folder holding the packs, created by the first export
	explicit ExportPanel(std::filesystem::path _folder);

	//Materials read from a splat map (GL_RGBA8 array, materials 4l..4l+3 in layer l); the texture must live until the export ends
	//@param names - every material in the order of their ids
	static MaterialSource SplatSource(const TextureClass& splat, std::vector<std::string> names);
	//Biomes of a table (id = index); colors read from the BIOMES layer (GL_RGBA32F), which must live until the export ends
	static BiomeSource BiomeTableSource(const std::vector<biome::Biome>& table, std::function<BiomeMap()> readIds, const TextureClass& colors);

	void ImGuiDraw(const Pack& pack);
	//Finishes an export whose background part is done; call every frame, the section may not be shown
	void Update() { PollBackground(); }

private:
	std::filesystem::path PackFolder() const { return folder / name; }
	std::string HeightFileName(const Target& target) const;
	//Writes a file of the pack and adds it to the manifest; on failure sets the status
	bool WritePng(const std::filesystem::path& packFolder, const exportpack::File& file, const std::vector<uint8_t>& pixels, int width, int height,
		int channels, exportpack::Manifest& manifest, std::string& saved);
	bool ExportMaterials(const MaterialSource& source, const std::filesystem::path& packFolder, exportpack::Manifest& manifest, std::string& saved);
	bool ExportBiomes(const BiomeSource& source, const std::filesystem::path& packFolder, exportpack::Manifest& manifest, std::string& saved);
	void ExportAll(const Pack& pack);
	void SetError(const std::string& fileName, const std::string& error, const std::string& saved);
	//Writes the manifest of a finished export and sets the status
	void Finish();
	//Takes the meshes and vegetation once they are written and finishes the export
	void PollBackground();

	//What the background thread writes; none = not asked for
	struct Background {
		std::optional<meshexport::Result> meshes;
		std::optional<vegexport::Result> vegetation;
	};

	//An export waiting for its background part, then written by Finish
	struct Pending {
		std::filesystem::path folder;
		exportpack::Manifest manifest;
		std::string saved;
		size_t clipped = 0;
		heightmap::Format format = heightmap::Format::PNG16;
	};
	std::optional<Pending> pending;
	GenerationJob<Background> job;

	std::filesystem::path folder;
	char name[65] = "";
	heightmap::Format format = heightmap::Format::PNG16;
	bool exportMaterials = true;
	bool exportBiomes = true;
	//0 = no meshes, then meshexport::Format + 1
	int meshFormat = 0;
	int lodLevels = 3;
	int tileChoice = 1;		//index in tileTexels
	bool exportWater = true;
	bool exportVegetation = false;
	bool exportModels = true;
	bool exportTextures = true;		//material textures of the materials on the map (with the material maps)
	bool exportScripts = true;		//engine importers (res/engine) and an R16 copy of the main map they read
	//Folder of the importer scripts, relative to the working directory (the resource folder)
	static constexpr const char* engineScriptsFolder = "res/engine";
	//Side of a mesh tile in map texels (metres = texels * Pack::metresPerTexel); 0 = whole map
	static constexpr float tileTexels[] = { 128.0f, 256.0f, 512.0f, 1024.0f, 0.0f };
	std::string status;
	bool statusIsError = false;
};
