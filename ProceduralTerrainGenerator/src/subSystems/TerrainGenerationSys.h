#pragma once

#include "glm/glm.hpp"

#include "utilities.h"
#include "TerrainGenerator.h"
#include "BiomeGenerator.h"
#include "Camera.h"
#include "LightSource.h"
#include "TerrainView.h"

class TerrainGenerationSys
{
private:
	int width, height;
	bool changeTerrain = false;
	bool biomesGeneration = false;
	bool editNoise = false, editSpline = false, infiniteGeneration = false;
	//true while an edited component noise (OVERLAY layer) is shown instead of the terrain
	bool showPreview = false;
	glm::vec3 oldCamPos = glm::vec3(0.0f);
	double lastGenerationMs = 0.0;

	//Rendering
	TerrainView view;
	TerrainRenderSettings renderSettings;

	//World parameters edited by the UI; generators only read them
	TerrainConfig terrainConfig = TerrainConfig::Default();
	BiomeConfig biomeConfig = BiomeConfig::Default();

	TerrainGenerator terrainGen;
	BiomeGenerator biomeGen;

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
	bool Resize();
	bool GenerateTerrain(float originx, float originy);
	bool GenerateBiomes();

	void Draw(Renderer& renderer, Camera& camera, LightSource& light);
	void ImGuiRightPanel();
	void ImGuiLeftPanel();
	void ImGuiOutput(glm::vec3 pos);
	void NoiseEditor();
	void BiomesEditor();
	void BiomeNoisesEditor();
	void SplineEditor();
	void NoisesLevelsForBiomes();
	void SegmentDrag(std::vector<float>& boundaries, std::string s);
};

