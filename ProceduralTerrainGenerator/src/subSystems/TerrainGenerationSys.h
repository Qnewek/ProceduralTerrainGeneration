#pragma once

#include <memory>
#include <vector>

#include "glm/glm.hpp"

#include "utilities.h"
#include "Biome.h"
#include "ChunkWorld.h"
#include "Grid.h"
#include "GpuHeightGenerator.h"
#include "WorldConfig.h"
#include "Camera.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "PresetPanel.h"
#include "ExportPanel.h"

//What has to be regenerated at the next Update; with infinite generation any request regenerates the chunks
struct WorldRequest {
	bool terrain = false;
	bool overlay = false;		//preview of the edited noise
	bool biomes = false;		//biome map and colors

	bool Any() const { return terrain || overlay || biomes; }
};

class TerrainGenerationSys
{
private:
	bool changeTerrain = false;
	bool biomesGeneration = false;
	bool editNoise = false, editSpline = false, infiniteGeneration = false;
	//true while an edited component noise (OVERLAY layer) is shown instead of the terrain
	bool showPreview = false;
	double lastGenerationMs = 0.0;

	//Rendering; the finite map lives in the layers of the view, the infinite world in chunks
	TerrainView view;
	ChunkWorld chunks;
	TerrainRenderSettings renderSettings;
	//infiniteGeneration as of the last Update
	bool chunksShown = false;
	//Noise the chunks show instead of the terrain, nullptr for the terrain
	const noise::NoiseConfigParameters* chunkPreview = nullptr;
	//Map size an island fills in the chunks: the last valid world size
	glm::ivec2 islandSize = glm::ivec2(0);

	//World parameters (map size, terrain, biomes) edited by the UI; generators only read them
	WorldConfig world;

	GpuHeightGenerator gpu;
	WorldRequest pending;

	std::vector<biome::Biome> biomeTable = biome::DefaultBiomes();
	std::unique_ptr<TextureClass> biomeIds;
	//Copy of biomeIds on the CPU, for the name of the biome under the camera
	BiomeMap biomeMap;

	//Currently edited noise (terrain or biome component), nullptr if none
	const noise::NoiseConfigParameters* EditedNoise() const;
	//Generates the currently edited noise into the OVERLAY layer
	void GenerateOverlay();
	//Infinite generation: turns requests into regenerating the chunks and generates the next chunks around the camera
	void UpdateChunks(const Camera& camera);
	//Finite map: generates what was requested
	void GenerateFinite();

	void RequestOverlay();
	void RequestTerrain();
	void RequestBiomes();

	void PresetsImGui();
	//Replaces the world parameters, resets editors holding copies of them and regenerates everything shown
	void ApplyWorld(const WorldConfig& loaded);
	PresetPanel presets{ "presets/world" };
	ExportPanel exporter{ "exports" };

	//Editor UI state
	WorldGenParameter editedComponent = WorldGenParameter::CONTINENTALNESS;
	BiomeParameter editedBiomeComponent = BiomeParameter::TEMPERATURE;
	int samplingResolution = 500;
	SplinePoints splinePlotPoints;	//copy of the edited spline, applied with "Set new spline points"

	//Pressed editor buttons (0 = nothing selected)
	int noisePressedButton = 0;
	int biomeNoisePressedButton = 0;
	int splinePressedButton = 0;
	int splineDraggedPoint = -1;

public:
	TerrainGenerationSys();
	~TerrainGenerationSys();
	
	bool Initialize(int _width, int _height, float _heightScale);
	//Called every frame before Draw: generates what was requested, or the chunks around the camera
	//@param camera - moved to the finite map when infinite generation is turned off
	void Update(Camera& camera);

	void Draw(Renderer& renderer, Camera& camera, LightSource& light);
	//true while the 2D map is shown instead of the 3D view
	bool FlatView() const { return renderSettings.map2d; }
	void ReloadShaders() { view.ReloadShader(); chunks.ReloadShader(); gpu.ReloadShaders(); }
	void ImGuiRightPanel();
	void ImGuiLeftPanel();
	void ImGuiOutput(glm::vec3 pos);
	void NoiseEditor();
	void BiomesEditor();
	void BiomeNoisesEditor();
	void SplineEditor();
	void NoisesLevelsForBiomes();
	bool SegmentDrag(std::vector<float>& boundaries, std::string s);
};

