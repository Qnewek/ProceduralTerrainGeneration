#pragma once

#include "VertexBufferLayout.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "TerrainGenerator.h"
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

	//OpenGl objects
	VertexBufferLayout layout;
	std::unique_ptr<VertexArray> mainVAO;
	std::unique_ptr<VertexBuffer> mainVertexBuffer;
	std::unique_ptr<VertexBuffer> noiseVertexBuffer;
	std::unique_ptr<IndexBuffer> mainIndexBuffer;
	std::unique_ptr<Shader> mainShader;

	TerrainGenerator terrainGen;
	TerrainGenerator::EvaluationMethod evaluatingMode = TerrainGenerator::EvaluationMethod::LINEAR_COMBINE;
public:
	TerrainGenerationSys();
	~TerrainGenerationSys();
	
	bool Initialize(unsigned int _height, unsigned int _width, float _heightScale);
	bool GenerateTerrain();

	void Draw(Renderer& renderer, Camera& camera, LightSource& light);
	void ImGuiDraw();
	void ImGuiOutput();
};

