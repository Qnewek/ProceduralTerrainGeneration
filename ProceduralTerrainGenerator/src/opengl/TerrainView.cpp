#include "TerrainView.h"
#include "ShadowMap.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "glm/gtc/matrix_transform.hpp"

#include "GpuHeightGenerator.h"
#include "TerrainMaterials.h"

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
	shader = CreateTerrainShader();
	if (!surfacePass.Initialize()) {
		return false;
	}
	layout.Push<float>(3);
	layout.Push<float>(2);

	width = _width;
	height = _height;
	BuildMesh();
	return true;
}

bool TerrainView::ReloadShader()
{
	surfacePass.ReloadShader();
	//A changed surface shader has to show without regenerating the maps
	surfaceOutdated[0] = surfaceOutdated[1] = true;
	return shader->Reload();
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

void TerrainView::BringCameraToMap(Camera& camera, const TerrainRenderSettings& settings) const
{
	const glm::vec2 half = glm::vec2(GetWorldWidth(), GetWorldHeight()) * 0.5f * settings.modelScale;
	const glm::vec3 position = camera.GetPosition();
	if (std::abs(position.x) <= half.x && std::abs(position.z) <= half.y) {
		return;
	}
	//Looks down at 35 degrees from 1.5 map radii away, or less if the far plane would cut the near half of the map off
	const float distance = std::min(std::max(half.x, half.y) * 1.5f, camera.GetRenderDistance() * 0.6f);
	const float elevation = glm::radians(35.0f);
	const glm::vec3 target(0.0f, 0.25f * settings.heightScale * settings.modelScale, 0.0f);
	camera.LookAt(target + glm::vec3(0.0f, std::sin(elevation), std::cos(elevation)) * distance, target);
}

std::unique_ptr<Shader> TerrainView::CreateTerrainShader()
{
	return std::make_unique<Shader>("res/shaders/TerrainShaders/Terrain_vertex.shader", "res/shaders/TerrainShaders/Terrain_fragment.shader",
		"res/shaders/TerrainShaders/Terrain_controlTes.shader", "res/shaders/TerrainShaders/Terrain_evalTes.shader");
}

namespace {
	//Sets view/projection of a top-down orthographic camera framing the given area
	void SetFlatCamera(FrameUniforms::Block& frame, const TerrainRenderSettings& settings, glm::vec2 flatMin, glm::vec2 flatMax)
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
		frame.view = glm::lookAt(glm::vec3(center.x, top, center.y), glm::vec3(center.x, 0.0f, center.y), glm::vec3(0.0f, 0.0f, -1.0f));
		frame.projection = glm::ortho(-half.x, half.x, -half.y, half.y, 0.0f, 2.0f * top);
	}
}

FrameUniforms::Block TerrainView::FrameBlock(Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, glm::vec2 flatMin, glm::vec2 flatMax)
{
	FrameUniforms::Block block = FrameUniforms::Make(camera, light);
	//The fog needs the perspective camera, so the 2D map has none; it ends where the far plane cuts the terrain off
	if (settings.map2d) {
		block.fogOn = 0;
		//Cloud shadows would only blot the map
		block.cloudLayer.w = 0.0f;
		SetFlatCamera(block, settings, flatMin, flatMax);
	}
	else if (settings.shadows) {
		settings.shadows->Apply(block);
	}
	return block;
}

void TerrainView::SetFrameUniforms(Shader& shader, FrameUniforms& frame, Camera& camera, LightSource& light, const TerrainRenderSettings& settings,
	const glm::mat4& model, int heightUnit, glm::vec2 flatMin, glm::vec2 flatMax)
{
	SetFrameUniforms(shader, frame, FrameBlock(camera, light, settings, flatMin, flatMax), settings, model, heightUnit);
}

void TerrainView::SetFrameUniforms(Shader& shader, FrameUniforms& frame, const FrameUniforms::Block& block, const TerrainRenderSettings& settings,
	const glm::mat4& model, int heightUnit)
{
	shader.Bind();
	frame.Upload(block);
	shader.SetModel(model);
	shader.SetUniformMat3f("normalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
	shader.SetUniform1i("displayMode", static_cast<int>(settings.displayMode));
	shader.SetUniform1f("step", settings.topoStep);
	shader.SetUniform1f("bandWidth", settings.topoBandWidth);
	shader.SetUniform1f("heightScale", settings.heightScale);
	shader.SetUniform2f("texelWorldSize", worldUnitsPerTexel, worldUnitsPerTexel);
	shader.SetUniform1f("meshScale", 1.0f);
	shader.SetUniform1f("skirtDepth", 0.0f);
	GLint viewport[4] = {};
	glGetIntegerv(GL_VIEWPORT, viewport);
	shader.SetUniform1i("screenSpaceTess", !settings.map2d);
	shader.SetUniform1f("tessPixels", settings.tessPixels);
	shader.SetUniform1f("viewportHeight", static_cast<float>(viewport[3]));
	shader.SetUniform1i("heightMap", heightUnit);
	shader.SetUniform1i("biomeMap", static_cast<int>(Layer::BIOMES));
	shader.SetUniform1i("surfaceMap", surfaceUnit);
	//The material arrays are bound once per frame by TerrainMaterials::Bind
	shader.SetUniform1i("materialAlbedo", TerrainMaterials::albedoUnit);
	shader.SetUniform1i("materialDetail", TerrainMaterials::detailUnit);
	shader.SetUniform1i("splatMap", splatUnit);
	shader.SetUniform1i("useSplat", 0);
}

TextureClass& TerrainView::SplatTexture(int _width, int _height)
{
	if (!splat || splat->GetWidth() != _width || splat->GetHeight() != _height) {
		splat = std::make_unique<TextureClass>(_width, _height, GL_RGBA8, GpuHeightGenerator::splatLayers);
	}
	return *splat;
}

void TerrainView::SetLayer(Layer layer, const HeightField& map)
{
	LayerTexture(layer, map.GetWidth(), map.GetHeight()).Upload(map.Data(), GL_RED, GL_FLOAT);
}

TextureClass& TerrainView::LayerTexture(Layer layer, int _width, int _height)
{
	std::unique_ptr<TextureClass>& texture = textures[static_cast<int>(layer)];
	if (!texture || texture->GetWidth() != _width || texture->GetHeight() != _height) {
		texture = std::make_unique<TextureClass>(_width, _height, layer == Layer::BIOMES ? GL_RGBA32F : GL_R32F);
	}
	if (layer != Layer::BIOMES) {
		surfaceOutdated[static_cast<int>(layer)] = true;
	}
	return *texture;
}

void TerrainView::UpdateSurface(Layer heightLayer)
{
	const int index = static_cast<int>(heightLayer);
	const TextureClass& heightMap = *textures[index];
	std::unique_ptr<TextureClass>& surface = surfaces[index];
	if (!surface || surface->GetWidth() != heightMap.GetWidth() || surface->GetHeight() != heightMap.GetHeight()) {
		surface = std::make_unique<TextureClass>(heightMap.GetWidth(), heightMap.GetHeight(), GL_RGBA32F);
		surfaceOutdated[index] = true;
	}
	if (surfaceOutdated[index]) {
		surfacePass.Run(heightMap, worldUnitsPerTexel, *surface);
		surfaceOutdated[index] = false;
	}
}

HeightField TerrainView::ReadLayer(Layer layer) const
{
	const TextureClass& texture = *textures[static_cast<int>(layer)];
	HeightField map(texture.GetWidth(), texture.GetHeight());
	texture.Read(GL_RED, GL_FLOAT, static_cast<int>(map.Size() * sizeof(float)), map.Data());
	return map;
}

void TerrainView::Draw(Renderer& renderer, Camera& camera, LightSource& light, const TerrainRenderSettings& settings,
	Layer heightLayer, glm::vec3 offset)
{
	if (!HasLayer(heightLayer)) {
		return;
	}
	//Before the terrain shader is bound: the pass binds its own program and textures
	UpdateSurface(heightLayer);

	glm::mat4 model = glm::mat4(1.0f);
	model = glm::scale(model, glm::vec3(settings.modelScale));
	model = glm::translate(model, offset);

	SetFrameUniforms(*shader, frameUniforms, camera, light, settings, model, static_cast<int>(heightLayer), flatMin, flatMax);

	for (int i = 0; i < 3; i++) {
		if (textures[i]) {
			textures[i]->Bind(i);
		}
	}
	surfaces[static_cast<int>(heightLayer)]->Bind(surfaceUnit);
	//The splat map belongs to the terrain, not to a noise preview drawn from OVERLAY
	if (splat && heightLayer == Layer::TERRAIN) {
		splat->Bind(splatUnit);
		shader->SetUniform1i("useSplat", 1);
	}

	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	renderer.DrawPatches(*vao, *shader, mapResolution * mapResolution, 4);
	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
}
