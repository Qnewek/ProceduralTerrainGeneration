#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "glm/glm.hpp"

#include "utilities.h"
#include "Biome.h"
#include "ChunkWorld.h"
#include "NoisePreview.h"
#include "Grid.h"
#include "GpuHeightGenerator.h"
#include "AsyncRead.h"
#include "GenerationJob.h"
#include "Hydrology.h"
#include "LandStats.h"
#include "RiverMap.h"
#include "WorldConfig.h"
#include "Camera.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "PresetPanel.h"
#include "ExportPanel.h"
#include "MapProject.h"
#include "PlacesPanel.h"
#include "PackPicker.h"
#include "BiomeEditor.h"
#include "PanelTabs.h"
#include "GrassLayer.h"
#include "VegetationLayer.h"
#include "ShadowMap.h"
#include "WaterSurface.h"
#include "GpuErosion.h"
#include "MapLabels.h"
#include "NestedSplineEditor.h"
#include "VegetationExport.h"

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

	//Rendering; the finite map lives in the layers of the view, the infinite world in chunks. With the partial map the chunks are
	//limited to it and not drawn: they feed its plants, grass and shadows
	TerrainView view;
	ChunkWorld chunks;
	//Vegetation on the chunks; the models belong to the application
	VegetationLayer vegetationLayer;
	VegetationModels* vegetationModels = nullptr;
	//Grass near the camera; grows on the materials of the application
	GrassLayer grass;
	//Sea, rivers and lakes
	WaterSurface water;
	//Sun shadows
	ShadowMap shadows;
	const TerrainMaterials* terrainMaterials = nullptr;
	TerrainRenderSettings renderSettings;
	//infiniteGeneration as of the last Update
	bool chunksShown = false;

	//The partial map is a part of the infinite world: world map position of its texel (0, 0). Leaving the infinite world puts it under
	//the camera; the generated map gets it with its next generation (TerrainView::GetMapOffset is the offset of the maps shown)
	glm::ivec2 mapOffset = glm::ivec2(0);
	//Partial map: centred again whenever the camera gets farther than a quarter of the map from its middle
	bool followCamera = false;
	//The Display tab asked to centre the map on the camera (it has no camera)
	bool centerOnCamera = false;
	//Partial map: plants, grass and sun shadows from chunks of the world limited to the map (ChunkWorld::SetBounds), as in the infinite world
	bool partialDetails = true;
	//Moves the partial map so its texel (0, 0) is world map position offset; regenerates it if that changes
	void MoveMap(glm::ivec2 offset);
	//Offset putting the middle of the map under the camera
	glm::ivec2 OffsetUnder(const Camera& camera) const;
	//Texels of the generated partial map in world map positions
	ChunkWorld::TexelRect MapRect() const;
	//Middle of the partial map being generated, in world map positions (the river region and land statistics are measured around it)
	glm::vec2 MapCenter() const { return glm::vec2(mapOffset) + glm::vec2(world.width, world.height) * 0.5f; }
	//Places plants on the ground chunks
	void UpdateVegetation();
	//Position of the partial map, in the Display tab
	void MapPositionImGui();

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

	//Rivers of the REGION method: the network of the region is computed in the background from a sample of the terrain (without
	//rivers) and uploaded to riverMap; the terrain is generated with the last uploaded network until a new one arrives
	struct RiverResult {
		hydrology::Network network;
		std::vector<hydrology::WaterVertex> water;
		double ms = 0.0;
		glm::vec2 center = glm::vec2(0.0f);		//of the region, map texels
	};
	//The network whose water WaterSurface draws: only while the REGION method is chosen
	const RiverMap* RiverWater() const { return world.rivers.method == rivers::Method::REGION && riverMap.Ready() ? &riverMap : nullptr; }
	GenerationJob<RiverResult> riverJob;
	//The terrain sample of the next network, read back without stalling the frame, and what the network needs with it
	AsyncRead riverSample;
	struct RiverSampleInput {
		int cells = 0;
		float cellSize = 1.0f, waterHeight = 0.0f, texelsPerHeight = 1.0f;
		glm::vec2 origin = glm::vec2(0.0f);
		glm::vec2 center = glm::vec2(0.0f);		//of the region, map texels
		rivers::RiverConfig config;
	} riverSampleInput;
	//Centre of the region of the network on screen (riverRegion is where the next one is computed); none before the first
	std::optional<glm::vec2> shownRiverCenter;
	RiverMap riverMap;
	//The uploaded network on the CPU, for the vegetation (nothing grows in rivers and lakes); shared with its placement threads
	std::shared_ptr<const hydrology::Network> riverNetwork;
	//The terrain, the biome modifiers, the map size or the river parameters changed since the last network was started
	bool riversStale = true;
	//Statistics of the uploaded network, for the Rivers tab
	int riverCells = 0, lakeCells = 0, lakeCount = 0;
	double riverMs = 0.0;
	//Infinite world: the region jumps after the camera over a grid of a quarter region (hydrology::SnapRegionCenter), not saved in presets
	bool riversFollowCamera = true;
	//Middle and grid step of the last network started (map texels); none = no network yet
	std::optional<hydrology::RegionPlace> riverRegion;
	//Map position (texels of the world) under the camera: inverse of the placement in OffsetUnder
	glm::vec2 MapPositionUnder(const Camera& camera) const;
	//Takes a finished network and starts a new one if needed: around the partial map, or in the infinite world around the camera
	//once it leaves the region (riversFollowCamera)
	void UpdateRivers(const Camera& camera);
	void RequestRivers() { riversStale = true; }
	//What the generation carves (the network may not be ready yet)
	RiverInput Rivers() const { return RiverInput{ world.rivers, &riverMap }; }

	//Erosion of the partial map only (a simulation, not part of the deterministic world): the eroded copy of the terrain lies in the OVERLAY
	//layer and is drawn next to the map along X, with its water and plants; biomes and materials stay those of the map
	GpuErosion gpuErosion;
	struct ErodedTerrain {
		bool shown = false;			//OVERLAY holds an eroded copy of the current terrain
		bool requested = false;		//"Erode map": erodes at the next Update (the copy again, or the terrain after a reset)
		bool plantsWanted = false;	//plants of the copy to place once no placement is running
		uint64_t version = 0;		//grows with every erosion and reset; plants placed for an older copy are dropped
		uint32_t runs = 0;			//seed of the droplets
		erosion::Method method = erosion::Method::DROPLETS;	//of the last erosion (label, export)
		double ms = 0.0, plantsMs = 0.0;
		bool treesOnly = false;		//the plants of the last placement are trees only (a large map)
		uint64_t plantSettings = 0;	//VegetationLayer::SettingsVersion the plants of the copy were placed with
	} eroded;
	struct ErodedPlants {
		std::vector<vegetation::Instance> instances;
		uint64_t version = 0;
		double ms = 0.0;
	};
	GenerationJob<ErodedPlants> erodedPlantsJob;
	//Above this many texels the eroded copy gets only trees: shrubs and rocks of a whole large map would take millions of instances
	static constexpr int64_t allPlantsMaxTexels = 2048ll * 2048ll;
	MapLabels mapLabels;
	//Gap between the map and its eroded copy: the copy is drawn shifted by this along X (world units before modelScale)
	float ErodedShift() const { return view.GetWorldWidth() * 1.05f; }
	//Erodes on request, installs the plants of the copy and starts placing them when wanted; partial map, after GenerateFinite
	void UpdateErosion();
	//Drops the eroded copy and its plants (any change of the terrain, leaving the partial map, Reset)
	void ResetErosion();
	//Reads the copy and its surface back and places its plants on a worker thread
	void StartErodedPlants();
	//true if a vegetation model can be placed
	bool AnyVegetationModel() const;
	//The plants of the map as the infinite world places them (the export, the eroded copy)
	vegexport::Input VegetationExportInput() const;
	void ErosionEditor();

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
	std::optional<mapproject::Project> sentMap;	//"Send map" pressed, waiting for TakeSentMap
	PlacesPanel places{ "places/world.json" };
	PackPicker packs{ "exports" };
	//Mode name in export packs
	static constexpr const char* packMode = "TerrainGen";
	//Presets are saved with the map position and loading one with a position moves the map there
	bool presetPosition = true;
	//Map offset of the current view: under the camera in the infinite world (where leaving it puts the map), else of the partial map
	glm::ivec2 PlaceOffset(const Camera& camera) const { return infiniteGeneration ? OffsetUnder(camera) : mapOffset; }
	//Moves the map to offset and the camera above it (a place without a camera: preset, pack)
	void GoToMap(glm::ivec2 offset, Camera& camera);
	void LoadPack(const exportpack::PackSource& source, Camera& camera);
	BiomeEditor biomeEditor;

	//Tabs of the right panel (generation); the open tab is the edited part, e.g. its noise gets the 2D picture
	static constexpr const char* tabTerrain = "Terrain";
	static constexpr const char* tabNoises = "Noises";
	static constexpr const char* tabSplines = "Splines";
	static constexpr const char* tabBiomes = "Biomes";
	static constexpr const char* tabBiomeTable = "Biome table";
	static constexpr const char* tabBiomeNoises = "Biome noises";
	static constexpr const char* tabRivers = "Rivers";
	static constexpr const char* tabErosion = "Erosion";
	PanelTabs generationTabs;

	//Editor UI state: the component each editor shows
	WorldGenParameter editedComponent = WorldGenParameter::CONTINENTALNESS;
	WorldGenParameter editedSplineComponent = WorldGenParameter::CONTINENTALNESS;
	BiomeParameter editedBiomeComponent = BiomeParameter::TEMPERATURE;
	int samplingResolution = 500;
	int splineDraggedPoint = -1;
	//The spline editor of the NESTED method
	NestedSplineEditor nestedEditor;

	//Content of the generation tabs
	void TerrainImGui();
	void LandStatsImGui();
	void NoiseEditor();
	void SplineEditor();
	//Switches the terrain method and regenerates; the first switch to NESTED makes its tree from the LAYERED curves
	void SetTerrainMethod(EvaluationMethod method);
	void BiomesEditor();
	void BiomeTableEditor();
	void BiomeNoisesEditor();
	void RiversEditor();
	//Names of the surface materials in the order of their ids (the river bed material)
	std::vector<std::string> materialNames;
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
	//Zoom of the 2D map for the editor camera, nullptr in 3D; the infinite world only zooms inside the area it shows
	cameramath::FlatZoom* FlatViewZoom()
	{
		renderSettings.flatZoom.fixedFrame = chunksShown;
		return renderSettings.map2d ? &renderSettings.flatZoom : nullptr;
	}
	//Infinite generation shows chunks with part of the view not generated yet
	bool HoleInView() const { return chunksShown && chunks.HasHoleInView(); }
	void ReloadShaders()
	{
		view.ReloadShader(); chunks.ReloadShader(); gpu.ReloadShaders(); noisePreview.ReloadShader(); vegetationLayer.ReloadShader(); grass.ReloadShader(); water.ReloadShader();
		gpuErosion.ReloadShaders();
	}
	//Names of the surface materials for the biome editor, in the order of their ids
	void SetMaterialNames(std::vector<std::string> names) { materialNames = names; biomeEditor.SetMaterialNames(std::move(names)); }
	//Models drawn on the infinite world; nullptr (before the models are destroyed) = no vegetation
	void SetVegetationModels(VegetationModels* models);
	//Placement settings and statistics, in the Vegetation tab
	void ImGuiVegetation() { vegetationLayer.ImGuiDraw(); grass.ImGuiDraw(); }
	//Materials the grass grows on (TerrainMaterial::grass); nullptr (before they are destroyed) = no grass
	void SetTerrainMaterials(const TerrainMaterials* materials) { terrainMaterials = materials; }
	//Right panel: the generation tabs
	void ImGuiRightPanel();
	//Content of the Display, Presets and Saving tabs of the left panel
	void ImGuiDisplay();
	//@param camera - saved with a place, moved by loading one
	void ImGuiPresets(Camera& camera);
	void ImGuiSaving();
	//A map sent to the Imported map mode from the Saving tab, once
	std::optional<mapproject::Project> TakeSentMap() { return std::exchange(sentMap, std::nullopt); }
	void ImGuiOutput(glm::vec3 pos);
	//Names of the map and its eroded copy over the scene of the last Draw; in the ImGui frame, after the panels
	void ImGuiMapLabels() const { mapLabels.ImGuiDraw(); }
};

