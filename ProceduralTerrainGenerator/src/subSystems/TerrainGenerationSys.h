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

	TerrainGenerator terrainGen;
	TerrainGenerator::WorldGenParameter editedComponent = TerrainGenerator::WorldGenParameter::CONTINENTALNESS;
	BiomeGenerator biomeGen;
	BiomeParameter editedBiomeComponent = BiomeParameter::TEMPERATURE;

	struct Point {
		float x, y;
	};
	std::vector<std::vector<double>> splinePlotPoints = {
		{-1.0, 0.0, 1.0},
		{0.0, 0.0, 0.0}
	};

	//Editor UI state (0 = nothing selected)
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

