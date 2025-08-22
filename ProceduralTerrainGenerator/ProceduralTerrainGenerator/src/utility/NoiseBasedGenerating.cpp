#include "NoiseBasedGenerating.h"

#include <iostream>

#include "utilities.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

NoiseBasedGenerating::NoiseBasedGenerating() : noise(), erosion(1, 1), vertices(nullptr), erosionVertices(nullptr), meshIndices(nullptr), wireFrame(false), erosionDraw(false), width(0), height(0), heightScale(255.0f), stride(6), seed(0)
{
}

NoiseBasedGenerating::~NoiseBasedGenerating()
{
	if (vertices) {
		delete[] vertices;
		vertices = nullptr;
	}
	if (meshIndices) {
		delete[] meshIndices;
		meshIndices = nullptr;
	}
	if(erosionVertices) {
		delete[] erosionVertices;
		erosionVertices = nullptr;
	}
}

bool NoiseBasedGenerating::Initialize(int height, int width, int seed, float heightScale)
{
	if(height <= 0 || width <= 0) {
		std::cout << "[ERROR] Invalid height or width value" << std::endl;
		return false;
	}
	this->height = height;
	this->width = width;
	this->seed = seed;
	this->heightScale = heightScale;

	noise.SetMapSize(width, height);
	noise.SetSeed(seed);
	noise.InitMap();

	vertices = new float[width * height * stride];
	meshIndices = new unsigned int[(height - 1) * width * 2]; // indices for strips

	utilities::benchmarkVoid(utilities::CreateTerrainMesh, "CreateTerrainMesh", noise, vertices, meshIndices, heightScale, stride, true, true);

	erosionVAO = std::make_unique<VertexArray>();
	mainVAO = std::make_unique<VertexArray>();
	mainVertexBuffer = std::make_unique<VertexBuffer>(vertices, (height * width) * stride * sizeof(float));
	mainIndexBuffer = std::make_unique<IndexBuffer>(meshIndices, (height - 1) * width * 2);
	mainShader = std::make_unique<Shader>("res/shaders/HeightMap_vertex.shader", "res/shaders/HeightMap_fragment.shader");
	layout.Push<float>(3);
	layout.Push<float>(3);
	mainVAO->AddBuffer(*mainVertexBuffer, layout);

	return true;
}

bool NoiseBasedGenerating::Resize(int _height, int _width) {
	if (height <= 0 || width <= 0) {
		std::cout << "[ERROR] Invalid height or width value" << std::endl;
		return false;
	}
	std::cout << "Resizing noise height map: " << height << " -> " << _height << ", " << width << " -> " << _width << std::endl;
	
	height = _height;
	width = _width;
	
	if (vertices) {
		delete[] vertices;
		vertices = new float[width * height * stride];
	}
	if (meshIndices) {
		delete[] meshIndices;
		meshIndices = new unsigned int[(height - 1) * width * 2];
	}
	
	noise.SetMapSize(width, height);
	noise.InitMap();
	utilities::benchmarkVoid(utilities::CreateTerrainMesh, "CreateTerrainMesh", noise, vertices, meshIndices, heightScale, stride, true, true);
	mainVertexBuffer->UpdateData(vertices, (height * width) * stride * sizeof(float));
	mainIndexBuffer->UpdateData(meshIndices, (height - 1) * width * 2 * sizeof(unsigned int));
	mainVAO->AddBuffer(*mainVertexBuffer, layout);
	return true;
}

bool NoiseBasedGenerating::SimulateErosion()
{
	if(width <=1 || height <= 1) {
		std::cout << "[ERROR] Invalid height or width value" << std::endl;
		return false;
	}
	if (!noise.GetMap()) {
		std::cout << "[ERROR] Noise map not initialized" << std::endl;
		return false;
	}
	if(erosionVertices) {
		delete[] erosionVertices;
	}
	erosionVertices = new float[height * width * stride];
	erosion.Resize(width, height);
	erosion.SetMap(noise.GetMap());
	utilities::benchmarkVoid(utilities::PerformErosion, "PerformErosion", erosion, erosionVertices, heightScale, std::nullopt, stride);
	erosionVertexBuffer = std::make_unique<VertexBuffer>(erosionVertices, (height * width) * stride * sizeof(float));
	erosionVAO->AddBuffer(*erosionVertexBuffer, layout);
	erosionDraw = true;

	return true;
}


void NoiseBasedGenerating::Draw(glm::mat4& model, Renderer& renderer, Camera& camera)
{
	mainShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 8.0f);
	mainShader->SetLightUniforms(glm::vec3(width, heightScale + 20.0f, height), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
	mainShader->SetViewPos(camera.GetPosition());
	mainShader->SetMVP(model, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());
	mainShader->SetUniform1f("scale", heightScale);

	if(wireFrame)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	renderer.DrawTriangleStrips(*mainVAO, *mainIndexBuffer, *mainShader, height - 1, width * 2);

	if (erosionDraw) {
		model = glm::translate(model, glm::vec3(width + 1.0f, 0.0f, 0.0f));
		mainShader->SetMVP(model, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());
		renderer.DrawTriangleStrips(*erosionVAO, *mainIndexBuffer, *mainShader, height - 1, width * 2);
	}
}
