#pragma once

#include <memory>
#include <vector>

#include "glm/glm.hpp"

#include "utilities.h"
#include "Biome.h"
#include "Grid.h"
#include "GpuHeightGenerator.h"
#include "WorldConfig.h"
#include "Camera.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "PresetPanel.h"
#include "ExportPanel.h"

//What has to be regenerated at the next Update
struct WorldRequest {
	bool terrain = false;
	float originx = 0.0f, originy = 0.0f;
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
	glm::vec3 oldCamPos = glm::vec3(0.0f);
	double lastGenerationMs = 0.0;

	//Rendering; generated maps live in the layers of the view
	TerrainView view;
	TerrainRenderSettings renderSettings;

	//World parameters (map size, terrain, biomes) edited by the UI; generators only read them
	WorldConfig world;

	GpuHeightGenerator gpu;
	WorldRequest pending;

	std::vector<biome::Biome> biomeTable = biome::DefaultBiomes();
	std::unique_ptr<TextureClass> biomeIds;
	//Copy of biomeIds on the CPU, for the name of the biome under the camera
	BiomeMap biomeMap;

	//Generates the currently edited noise (terrain or biome component) into the OVERLAY layer
	void GenerateOverlay();

	void RequestOverlay();
	void RequestTerrain(float originx, float originy);
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
	//Called every frame before Draw: generates what was requested
	void Update();

	void Draw(Renderer& renderer, Camera& camera, LightSource& light);
	void ReloadShaders() { view.ReloadShader(); gpu.ReloadShaders(); }
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

