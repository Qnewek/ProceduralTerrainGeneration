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
	float* terrainVertices;
	float heightScale, modelScale;
	unsigned int* terrainIndices;
	unsigned int stride, width, height;
	bool newSize = true;

	//OpenGl objects
	VertexBufferLayout layout;
	std::unique_ptr<VertexArray> mainVAO;
	std::unique_ptr<VertexBuffer> mainVertexBuffer;
	std::unique_ptr<IndexBuffer> mainIndexBuffer;
	std::unique_ptr<Shader> mainShader;

	TerrainGenerator terrainGen;
public:
	TerrainGenerationSys();
	~TerrainGenerationSys();
	
	bool Initialize(unsigned int _height, unsigned int _width, float _heightScale);
	bool GenerateTerrain();

	void Draw(Renderer& renderer, Camera& camera, LightSource& light);
	void ImGuiDraw();
};

