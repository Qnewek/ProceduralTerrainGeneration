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
	float* terrainVertices, *noiseVertices;
	float heightScale, modelScale;
	unsigned int* terrainIndices;
	unsigned int stride;
	int width, height;
	bool wireFrame = false, changeTerrain = false;
	bool biomesGeneration = false, map2d = false;
	bool editSpline = false;

	//OpenGl objects
	VertexBufferLayout layout;
	std::unique_ptr<VertexArray> mainVAO;
	std::unique_ptr<VertexBuffer> mainVertexBuffer;
	std::unique_ptr<VertexBuffer> noiseVertexBuffer;
	std::unique_ptr<IndexBuffer> mainIndexBuffer;
	std::unique_ptr<Shader> mainShader;

	BiomeGenerator biomeGen;
	TerrainGenerator terrainGen;
	TerrainGenerator::EvaluationMethod evaluatingMode = TerrainGenerator::EvaluationMethod::LINEAR_COMBINE;
	TerrainGenerator::WorldGenParameter editedSpline = TerrainGenerator::WorldGenParameter::CONTINENTALNESS;
	utilities::heightMapMode displayMode = utilities::heightMapMode::TOPOGRAPHICAL;

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
	bool GenerateTerrain();
	bool GenerateBiomes();
	void UpdateVertex(std::unique_ptr<VertexBuffer>& vb, float* v);

	void Draw(Renderer& renderer, Camera& camera, LightSource& light);
	void ImGuiRightPanel();
	void ImGuiLeftPanel();
	void ImGuiOutput();
	void BiomesImGui();
	void SplineEditor();
};

