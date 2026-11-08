#pragma once

#include <memory>
#include <vector>

#include "glm/glm.hpp"

#include "utilities.h"
#include "Biome.h"
#include "ChunkWorld.h"
#include "NoisePreview.h"
#include "Grid.h"
#include "GpuHeightGenerator.h"
#include "WorldConfig.h"
#include "Camera.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "PresetPanel.h"
#include "ExportPanel.h"

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
	bool editNoise = false, editSpline = false, infiniteGeneration = false;
	//The edited noise is shown as a 2D picture in the panel; the view always shows the resulting terrain
	bool showNoisePreview = true;
	NoisePreview noisePreview;
	double lastGenerationMs = 0.0;

	//Rendering; the finite map lives in the layers of the view, the infinite world in chunks
	TerrainView view;
	ChunkWorld chunks;
	TerrainRenderSettings renderSettings;
	//infiniteGeneration as of the last Update
	bool chunksShown = false;

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
	//Generates the 2D picture of the edited noise if it was requested
	void GeneratePreview();
	//Infinite generation: turns requests into regenerating the chunks and generates the next chunks around the camera
	void UpdateChunks(const Camera& camera);
	//Finite map: generates what was requested
	void GenerateFinite();

	void RequestPreview();
	void RequestTerrain();
	void RequestBiomes();

	void PresetsImGui();
	//Replaces the world parameters, resets editors holding copies of them and regenerates everything shown
	void ApplyWorld(const WorldConfig& loaded);
	PresetPanel presets{ "presets/world" };
	ExportPanel exporter{ "exports" };

	//Editor UI state; the noise and spline editors are open together, so each has its own component
	WorldGenParameter editedComponent = WorldGenParameter::CONTINENTALNESS;
	WorldGenParameter editedSplineComponent = WorldGenParameter::CONTINENTALNESS;
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
	//Infinite generation shows chunks with part of the view not generated yet
	bool HoleInView() const { return chunksShown && chunks.HasHoleInView(); }
	void ReloadShaders() { view.ReloadShader(); chunks.ReloadShader(); gpu.ReloadShaders(); noisePreview.ReloadShader(); }
	void ImGuiRightPanel();
	void ImGuiLeftPanel();
	void ImGuiOutput(glm::vec3 pos);
	void NoiseEditor();
	//Checkbox and 2D picture of the edited noise, shown only in the editor of that noise
	void NoisePreviewImGui(const noise::NoiseConfigParameters& noise);
	void BiomesEditor();
	void BiomeNoisesEditor();
	void SplineEditor();
	void NoisesLevelsForBiomes();
	bool SegmentDrag(std::vector<float>& boundaries, std::string s);
};

