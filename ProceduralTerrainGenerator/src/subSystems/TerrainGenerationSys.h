#pragma once

#include "VertexBufferLayout.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "utilities.h"
#include "TerrainGenerator.h"
#include "BiomeGenerator.h"
#include "Camera.h"
#include "LightSource.h"

class TerrainGenerationSys
{
private:
	float* terrainVertices;
	float heightScale, modelScale;
	unsigned int stride;
	int width, height, mapResolution;
	bool wireFrame = false, changeTerrain = false;
	bool biomesGeneration = false, map2d = false;
	bool editNoise = false, editSpline = false, infiniteGeneration = false;
	glm::vec3 oldCamPos = glm::vec3(0.0f);

	//OpenGl objects
	VertexBufferLayout layout;
	std::unique_ptr<VertexArray> mainVAO;
	std::unique_ptr<VertexBuffer> mainVertexBuffer;
	std::unique_ptr<Shader> mainShader;
	std::unique_ptr<TextureClass> terrainTxt;
	std::unique_ptr<TextureClass> noiseTxt;
	std::unique_ptr<TextureClass> biomeTxt;

	TerrainGenerator terrainGen;
	TerrainGenerator::EvaluationMethod evaluatingMode = TerrainGenerator::EvaluationMethod::LINEAR_COMBINE;
	TerrainGenerator::WorldGenParameter editedComponent = TerrainGenerator::WorldGenParameter::CONTINENTALNESS;
	utilities::heightMapMode displayMode = utilities::heightMapMode::TOPOGRAPHICAL;
	BiomeGenerator biomeGen;
	BiomeParameter editedBiomeComponent = BiomeParameter::TEMPERATURE;

	struct Point {
		float x, y;
	};
	std::vector<std::vector<double>> splinePlotPoints = {
		{-1.0, 0.0, 1.0},
		{0.0, 0.0, 0.0}
	};

public:
	TerrainGenerationSys();
	~TerrainGenerationSys();
	
	bool Initialize(unsigned int _height, unsigned int _width, float _heightScale);
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

