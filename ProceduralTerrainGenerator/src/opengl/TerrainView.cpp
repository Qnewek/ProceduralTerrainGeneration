#include "TerrainView.h"

#include <algorithm>
#include <cmath>
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
	ResetFlatViewBounds();
}

void TerrainView::ResetFlatViewBounds()
{
	glm::vec2 half(GetWorldWidth() / 2.0f, GetWorldHeight() / 2.0f);
	SetFlatViewBounds(-half, half);
}

void TerrainView::SetFlatCameraUniforms(const TerrainRenderSettings& settings)
{
	//Bounds are given before modelScale; model = scale * translate(offset), so scaling them is enough
	glm::vec2 min = flatMin * settings.modelScale;
	glm::vec2 max = flatMax * settings.modelScale;
	glm::vec2 center = (min + max) * 0.5f;
	glm::vec2 half = (max - min) * 0.5f * 1.02f;	//small margin

	//Keep the aspect ratio of the viewport
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	float aspect = viewport[3] > 0 ? static_cast<float>(viewport[2]) / viewport[3] : 1.0f;
	if (half.x < half.y * aspect) {
		half.x = half.y * aspect;
	}
	else {
		half.y = half.x / aspect;
	}

	//Camera above the highest possible point looking straight down; +X to the right, -Z up
	float top = std::abs(settings.heightScale * settings.modelScale) + 1.0f;
	glm::mat4 flatView = glm::lookAt(glm::vec3(center.x, top, center.y), glm::vec3(center.x, 0.0f, center.y), glm::vec3(0.0f, 0.0f, -1.0f));
	glm::mat4 flatProjection = glm::ortho(-half.x, half.x, -half.y, half.y, 0.0f, 2.0f * top);
	shader->SetView(flatView);
	shader->SetProjection(flatProjection);
}

void TerrainView::SetLayer(Layer layer, const HeightField& map)
{
	UploadLayer(layer, map.Data(), map.GetWidth(), map.GetHeight(), GL_R32F, GL_RED);
}

void TerrainView::SetBiomeColors(const std::vector<glm::vec3>& colors, int _width, int _height)
{
	UploadLayer(Layer::BIOMES, colors.data(), _width, _height, GL_RGB32F, GL_RGB);
}

void TerrainView::UploadLayer(Layer layer, const void* data, int _width, int _height, GLenum internalFormat, GLenum format)
{
	std::unique_ptr<TextureClass>& texture = textures[static_cast<int>(layer)];
	if (!texture || texture->GetWidth() != _width || texture->GetHeight() != _height) {
		texture = std::make_unique<TextureClass>(_width, _height, internalFormat);
	}
	texture->Upload(data, format, GL_FLOAT);
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
	if (settings.map2d) {
		SetFlatCameraUniforms(settings);
	}
	shader->SetModel(model);
	shader->SetUniformMat3f("normalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
	shader->SetUniform1i("displayMode", static_cast<int>(settings.displayMode));
	shader->SetUniform1f("step", settings.topoStep);
	shader->SetUniform1f("bandWidth", settings.topoBandWidth);
	shader->SetUniform1f("heightScale", settings.heightScale);
	shader->SetUniform2f("texelWorldSize", worldUnitsPerTexel, worldUnitsPerTexel);
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
