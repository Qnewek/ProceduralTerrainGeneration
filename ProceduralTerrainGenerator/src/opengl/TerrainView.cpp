#include "TerrainView.h"

#include <algorithm>
#include <iostream>

#include "glm/gtc/matrix_transform.hpp"

TerrainView::TerrainView()
{
}

TerrainView::~TerrainView()
{
}

bool TerrainView::Initialize(int _width, int _height)
{
	if (_width <= 0 || _height <= 0) {
		std::cout << "[ERROR] TerrainView: invalid map size " << _width << "x" << _height << std::endl;
		return false;
	}
	vao = std::make_unique<VertexArray>();
	shader = std::make_unique<Shader>("res/shaders/TerrainShaders/Terrain_vertex.shader", "res/shaders/TerrainShaders/Terrain_fragment.shader", "res/shaders/TerrainShaders/Terrain_controlTes.shader", "res/shaders/TerrainShaders/Terrain_evalTes.shader");
	layout.Push<float>(3);
	layout.Push<float>(2);

	width = _width;
	height = _height;
	BuildMesh();
	return true;
}

bool TerrainView::Resize(int _width, int _height)
{
	if (_width == width && _height == height) {
		return false;
	}
	width = _width;
	height = _height;
	BuildMesh();
	return true;
}

//Rebuilds the patch grid so that it spans the current map size (GetWorldWidth() along X, GetWorldHeight() along Z)
void TerrainView::BuildMesh()
{
	vertices.assign(static_cast<size_t>(mapResolution) * mapResolution * stride * 4, 0.0f);
	utilities::GenerateVerticesForResolution(vertices.data(), static_cast<int>(GetWorldWidth()), static_cast<int>(GetWorldHeight()), mapResolution, stride, 0, 3);
	vertexBuffer = std::make_unique<VertexBuffer>(vertices.data(), static_cast<unsigned int>(vertices.size() * sizeof(float)));
	vao->AddBuffer(*vertexBuffer, layout);
}

void TerrainView::SetLayer(Layer layer, const HeightField& map)
{
	textures[static_cast<int>(layer)] = std::make_unique<TextureClass>(map.Data(), map.GetWidth(), map.GetHeight());
}

void TerrainView::SetBiomeColors(const std::vector<glm::vec3>& colors, int _width, int _height)
{
	textures[static_cast<int>(Layer::BIOMES)] = std::make_unique<TextureClass>(colors, _width, _height);
}

void TerrainView::Draw(Renderer& renderer, Camera& camera, LightSource& light, const TerrainRenderSettings& settings,
	Layer heightLayer, glm::vec3 offset)
{
	if (!HasLayer(heightLayer)) {
		return;
	}

	glm::mat4 model = glm::mat4(1.0f);
	model = glm::scale(model, glm::vec3(settings.modelScale));
	model = glm::translate(model, offset);

	shader->Bind();
	light.SetLightUniforms(*shader);
	camera.SetUniforms(*shader);
	shader->SetModel(model);
	shader->SetUniform1i("displayMode", static_cast<int>(settings.displayMode));
	shader->SetUniform1f("step", settings.topoStep);
	shader->SetUniform1f("bandWidth", settings.topoBandWidth);
	shader->SetUniform1i("size", std::max(width, height) / 2);
	shader->SetUniform1i("flatten", settings.map2d);
	shader->SetUniform1f("heightScale", settings.heightScale);
	shader->SetUniform1i("heightMap", static_cast<int>(heightLayer));
	shader->SetUniform1i("biomeMap", static_cast<int>(Layer::BIOMES));

	for (int i = 0; i < 3; i++) {
		if (textures[i]) {
			textures[i]->Bind(i);
		}
	}

	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	renderer.DrawPatches(*vao, *shader, mapResolution * mapResolution, 4);
	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
}
