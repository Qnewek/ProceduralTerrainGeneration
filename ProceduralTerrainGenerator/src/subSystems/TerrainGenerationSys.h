#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "glm/glm.hpp"

#include "utilities.h"
#include "Biome.h"
#include "ChunkWorld.h"
#include "NoisePreview.h"
#include "Grid.h"
#include "GpuHeightGenerator.h"
#include "LandStats.h"
#include "WorldConfig.h"
#include "Camera.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "PresetPanel.h"
#include "ExportPanel.h"
#include "BiomeEditor.h"
#include "PanelTabs.h"
#include "VegetationLayer.h"

//What has to be regenerated at the next Update; with infinite generation a terrain or biome request regenerates the chunks
struct WorldRequest {
	bool terrain = false;
	bool biomes = false;		//biome map and colors
	bool preview = false;		//2D picture of the edited noise; does not touch the terrain or the chunks

	bool Any() const { return terrain || biomes; }
};

class TerrainGenerationSys
{
private:
	bool changeTerrain = false;
	bool biomesGeneration = false;
	bool infiniteGeneration = true;
	//The edited noise is shown as a 2D picture in the panel; the view always shows the resulting terrain
	bool showNoisePreview = true;
	NoisePreview noisePreview;
	double lastGenerationMs = 0.0;

	//Rendering; the finite map lives in the layers of the view, the infinite world in chunks
	TerrainView view;
	ChunkWorld chunks;
	//Vegetation on the chunks; the models belong to the application
	VegetationLayer vegetationLayer;
	VegetationModels* vegetationModels = nullptr;
	TerrainRenderSettings renderSettings;
	//infiniteGeneration as of the last Update
	bool chunksShown = false;

	//World parameters (map size, terrain, biomes) edited by the UI; generators only read them
	WorldConfig world;

	GpuHeightGenerator gpu;
	WorldRequest pending;

	std::unique_ptr<TextureClass> biomeIds;
	//Copy of biomeIds on the CPU, for the name of the biome under the camera
	BiomeMap biomeMap;

	//Land statistics of the Terrain tab: measured on request in Update (reads the GPU back)
	std::optional<LandStats> landStats;
	bool measureLand = false, landStatsOld = false;

	//Currently edited noise (terrain or biome component), nullptr if none
	const noise::NoiseConfigParameters* EditedNoise() const;
	//Generates the 2D picture of the edited noise if it was requested
	void GeneratePreview();
	//Infinite generation: turns requests into regenerating the chunks and generates the next chunks around the camera
	void UpdateChunks(const Camera& camera);
	//Finite map: generates what was requested
	void GenerateFinite();
	void MeasureLand();

	void RequestPreview();
	void RequestTerrain();
	void RequestBiomes();

	//Replaces the world parameters, resets editors holding copies of them and regenerates everything shown
	void ApplyWorld(const WorldConfig& loaded);
	PresetPanel presets{ "presets/world" };
	ExportPanel exporter{ "exports" };
	BiomeEditor biomeEditor;

	//Tabs of the right panel (generation); the open tab is the edited part, e.g. its noise gets the 2D picture
	static constexpr const char* tabTerrain = "Terrain";
	static constexpr const char* tabNoises = "Noises";
	static constexpr const char* tabSplines = "Splines";
	static constexpr const char* tabBiomes = "Biomes";
	static constexpr const char* tabBiomeTable = "Biome table";
	static constexpr const char* tabBiomeNoises = "Biome noises";
	PanelTabs generationTabs;

	//Editor UI state: the component each editor shows
	WorldGenParameter editedComponent = WorldGenParameter::CONTINENTALNESS;
	WorldGenParameter editedSplineComponent = WorldGenParameter::CONTINENTALNESS;
	BiomeParameter editedBiomeComponent = BiomeParameter::TEMPERATURE;
	int samplingResolution = 500;
	int splineDraggedPoint = -1;

	//Content of the generation tabs
	void TerrainImGui();
	void LandStatsImGui();
	void NoiseEditor();
	void SplineEditor();
	void BiomesEditor();
	void BiomeTableEditor();
	void BiomeNoisesEditor();
	//Checkbox and 2D picture of the edited noise, shown only in the editor of that noise
	void NoisePreviewImGui(const noise::NoiseConfigParameters& noise);
	//Shows a note instead of a biome tab while biomes are off
	//@return true if biomes are off
	bool BiomesOffNotice();

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
	//Infinite generation shows chunks with part of the view not generated yet
	bool HoleInView() const { return chunksShown && chunks.HasHoleInView(); }
	void ReloadShaders() { view.ReloadShader(); chunks.ReloadShader(); gpu.ReloadShaders(); noisePreview.ReloadShader(); }
	//Names of the surface materials for the biome editor, in the order of their ids
	void SetMaterialNames(std::vector<std::string> names) { biomeEditor.SetMaterialNames(std::move(names)); }
	//Models drawn on the infinite world; nullptr (before the models are destroyed) = no vegetation
	void SetVegetationModels(VegetationModels* models);
	//Placement settings and statistics, in the Vegetation tab
	void ImGuiVegetation() { vegetationLayer.ImGuiDraw(); }
	//Right panel: the generation tabs
	void ImGuiRightPanel();
	//Content of the Display, Presets and Saving tabs of the left panel
	void ImGuiDisplay();
	void ImGuiPresets();
	void ImGuiSaving();
	void ImGuiOutput(glm::vec3 pos);
};

