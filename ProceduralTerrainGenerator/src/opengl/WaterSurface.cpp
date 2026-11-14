#include "WaterSurface.h"

#include <iostream>

#include "glm/gtc/matrix_transform.hpp"
#include "imgui.h"

#include "Camera.h"
#include "ChunkWorld.h"
#include "LightSource.h"
#include "Shader.h"
#include "TerrainView.h"
#include "TextureClass.h"
#include "VegetationPlacement.h"

namespace {
	//Texture units while drawing water: height map under it, copies of the scene color and depth
	constexpr int heightUnit = 11, sceneColorUnit = 12, sceneDepthUnit = 13;
}

WaterSurface::WaterSurface() = default;

WaterSurface::~WaterSurface()
{
	if (vao) {
		glDeleteVertexArrays(1, &vao);
	}
	if (sceneColor) {
		glDeleteTextures(1, &sceneColor);
	}
	if (sceneDepth) {
		glDeleteTextures(1, &sceneDepth);
	}
}

bool WaterSurface::ReloadShader()
{
	return !shader || shader->Reload();
}

void WaterSurface::Draw(const ChunkWorld& chunks, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, float waterLevel)
{
	if (!enabled || failed) {
		return;
	}
	quads.clear();
	const glm::vec2 mapOrigin = chunks.GetMapOrigin();
	//Chunk texel t is texel t + 1 of its maps, sampled at its center (as the terrain mesh)
	const float size = static_cast<float>(ChunkWorld::textureSize);
	const glm::vec2 uvMin(1.5f / size), uvMax((ChunkWorld::chunkSize + 1.5f) / size);
	for (const ChunkWorld::GroundChunk& g : chunks.GroundChunks(ChunkWorld::maxLevel, camera.GetRenderDistance() / settings.modelScale)) {
		if (!g.height) {
			continue;
		}
		const float side = static_cast<float>(ChunkWorld::chunkSize * (1 << g.level)) * TerrainView::worldUnitsPerTexel;
		quads.push_back({ g.height, mapOrigin + glm::vec2(g.coord) * side, glm::vec2(side), uvMin, uvMax });
	}
	DrawQuads(quads, camera, light, settings, waterLevel, chunks.FlatViewMin(), chunks.FlatViewMax());
}

void WaterSurface::DrawMap(const TextureClass& heightMap, glm::vec2 size, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings,
	float waterLevel, glm::vec2 flatMin, glm::vec2 flatMax)
{
	if (!enabled || failed) {
		return;
	}
	quads.clear();
	//Texture coordinates run over the whole map as on the terrain mesh (TerrainView::BuildMesh)
	quads.push_back({ &heightMap, -size * 0.5f, size, glm::vec2(0.0f), glm::vec2(1.0f) });
	DrawQuads(quads, camera, light, settings, waterLevel, flatMin, flatMax);
}

bool WaterSurface::CopyScene(glm::ivec4& viewport)
{
	glGetIntegerv(GL_VIEWPORT, &viewport.x);
	if (viewport.z <= 0 || viewport.w <= 0) {
		return false;
	}
	if (sceneSize != glm::ivec2(viewport.z, viewport.w)) {
		if (sceneColor) {
			glDeleteTextures(1, &sceneColor);
			glDeleteTextures(1, &sceneDepth);
		}
		sceneSize = glm::ivec2(viewport.z, viewport.w);
		glCreateTextures(GL_TEXTURE_2D, 1, &sceneColor);
		glTextureStorage2D(sceneColor, 1, GL_RGBA8, sceneSize.x, sceneSize.y);
		glCreateTextures(GL_TEXTURE_2D, 1, &sceneDepth);
		glTextureStorage2D(sceneDepth, 1, GL_DEPTH_COMPONENT24, sceneSize.x, sceneSize.y);
		for (const GLuint texture : { sceneColor, sceneDepth }) {
			const GLint filter = texture == sceneColor ? GL_LINEAR : GL_NEAREST;
			glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, filter);
			glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, filter);
			glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
	}
	glCopyTextureSubImage2D(sceneColor, 0, 0, 0, viewport.x, viewport.y, viewport.z, viewport.w);
	glCopyTextureSubImage2D(sceneDepth, 0, 0, 0, viewport.x, viewport.y, viewport.z, viewport.w);
	return true;
}

void WaterSurface::DrawQuads(const std::vector<Quad>& list, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings,
	float waterLevel, glm::vec2 flatMin, glm::vec2 flatMax)
{
	if (list.empty()) {
		return;
	}
	if (!shader) {
		shader = std::make_unique<Shader>("res/shaders/WaterShaders/water_vertex.shader", "res/shaders/WaterShaders/water_fragment.shader");
		if (!shader->IsValid()) {
			std::cout << "[ERROR] WaterSurface: cannot build the water shader" << std::endl;
			failed = true;
			return;
		}
		//The quad comes from the vertex index, but a draw needs a vertex array
		glCreateVertexArrays(1, &vao);
	}
	glm::ivec4 viewport;
	if (!CopyScene(viewport)) {
		return;
	}

	//In the 2D map the same top-down camera as the terrain
	const FrameUniforms::Block block = TerrainView::FrameBlock(camera, light, settings, flatMin, flatMax);
	shader->Bind();
	frameUniforms.Upload(block);
	shader->SetModel(glm::scale(glm::mat4(1.0f), glm::vec3(settings.modelScale)));
	shader->SetUniform1i("flatView", settings.map2d);
	shader->SetUniform1i("heightMap", heightUnit);
	shader->SetUniform1i("sceneColor", sceneColorUnit);
	shader->SetUniform1i("sceneDepth", sceneDepthUnit);
	shader->SetUniformMat4f("inverseProjection", glm::inverse(block.projection));
	shader->SetUniform4f("viewportRect", static_cast<float>(viewport.x), static_cast<float>(viewport.y), static_cast<float>(viewport.z),
		static_cast<float>(viewport.w));
	shader->SetUniform1f("heightScale", settings.heightScale);
	shader->SetUniform1f("waterLevel", waterLevel);
	shader->SetUniform1f("unitsPerMetre", vegetation::unitsPerMetre);
	shader->SetUniform1f("modelScale", settings.modelScale);
	shader->SetUniform1f("clarity", clarity);
	shader->SetUniform1f("waveStrength", waveStrength);
	shader->SetUniform1f("refraction", refraction);
	shader->SetUniform1i("reflections", reflections && !settings.map2d);
	shader->SetUniform3fv("shallowColor", shallowColor);
	shader->SetUniform3fv("deepColor", deepColor);
	glBindTextureUnit(sceneColorUnit, sceneColor);
	glBindTextureUnit(sceneDepthUnit, sceneDepth);

	//The shader mixes in the scene under the water itself, so the result is opaque
	const GLboolean culling = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);
	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	glBindVertexArray(vao);
	for (const Quad& q : list) {
		q.height->Bind(heightUnit);
		shader->SetUniform2f("corner", q.corner.x, q.corner.y);
		shader->SetUniform2f("size", q.size.x, q.size.y);
		shader->SetUniform2f("uvMin", q.uvMin.x, q.uvMin.y);
		shader->SetUniform2f("uvMax", q.uvMax.x, q.uvMax.y);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	glBindVertexArray(0);
	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	glDepthMask(GL_TRUE);
	if (culling) {
		glEnable(GL_CULL_FACE);
	}
}

void WaterSurface::ImGuiDraw()
{
	ImGui::SeparatorText("Water");
	ImGui::Checkbox("Water surface", &enabled);
	ImGui::SetItemTooltip("Sea surface at the water level of the terrain (3D and 2D map)");
	if (!enabled) {
		return;
	}
	ImGui::Checkbox("Reflections", &reflections);
	ImGui::SetItemTooltip("Terrain and plants reflected in the water, traced through the image on screen;\n"
		"what is off screen reflects the sky instead");
	ImGui::SliderFloat("Clarity", &clarity, 0.5f, 20.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
	ImGui::SetItemTooltip("Water along the view through which about a third of the ground under it still shows");
	ImGui::SliderFloat("Waves", &waveStrength, 0.0f, 3.0f, "%.2f");
	ImGui::SliderFloat("Refraction", &refraction, 0.0f, 3.0f, "%.2f");
	ImGui::SetItemTooltip("How much the waves shift the ground seen under the water");
	ImGui::ColorEdit3("Shallow water", &shallowColor.x);
	ImGui::ColorEdit3("Deep water", &deepColor.x);
}
