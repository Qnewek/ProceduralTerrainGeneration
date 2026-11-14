#include "GrassLayer.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "glm/gtc/matrix_transform.hpp"
#include "imgui.h"

#include "Camera.h"
#include "ChunkWorld.h"
#include "Frustum.h"
#include "LightSource.h"
#include "Shader.h"
#include "TerrainMaterials.h"
#include "TerrainView.h"
#include "VegetationPlacement.h"

namespace {
	//Texture units of the chunk maps while placing (the terrain binds its own before every draw)
	constexpr int heightUnit = 11, surfaceUnit = 12, splatUnit = 13;
	constexpr int bladeBytes = 32;
	constexpr GLuint emptyCommand[4] = { GrassLayer::bladeVertices, 0, 0, 0 };
}

GrassLayer::GrassLayer() = default;

GrassLayer::~GrassLayer()
{
	const GLuint buffers[] = { bladeBuffer, commandBuffer, statsBuffer };
	for (GLuint b : buffers) {
		if (b) {
			glDeleteBuffers(1, &b);
		}
	}
	if (vao) {
		glDeleteVertexArrays(1, &vao);
	}
}

bool GrassLayer::Initialize()
{
	placeShader = Shader::Compute("res/shaders/VegetationShaders/grass_compute.shader");
	drawShader = std::make_unique<Shader>("res/shaders/VegetationShaders/grass_vertex.shader", "res/shaders/VegetationShaders/grass_fragment.shader");
	if (!placeShader->IsValid() || !drawShader->IsValid()) {
		std::cout << "[ERROR] GrassLayer: cannot build the grass shaders" << std::endl;
		return false;
	}
	glCreateBuffers(1, &bladeBuffer);
	glNamedBufferStorage(bladeBuffer, static_cast<GLsizeiptr>(capacity) * bladeBytes, nullptr, 0);
	glCreateBuffers(1, &commandBuffer);
	glNamedBufferStorage(commandBuffer, sizeof(emptyCommand), emptyCommand, GL_DYNAMIC_STORAGE_BIT);
	glCreateBuffers(1, &statsBuffer);
	glNamedBufferStorage(statsBuffer, sizeof(emptyCommand), emptyCommand, GL_DYNAMIC_STORAGE_BIT | GL_MAP_READ_BIT);
	//Blades come from the buffer, not from attributes, but a draw needs a vertex array
	glCreateVertexArrays(1, &vao);
	return true;
}

bool GrassLayer::ReloadShader()
{
	bool ok = true;
	if (placeShader) {
		ok = placeShader->Reload() && ok;
	}
	if (drawShader) {
		ok = drawShader->Reload() && ok;
	}
	return ok;
}

void GrassLayer::Draw(const ChunkWorld& chunks, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings,
	const TerrainMaterials& materials)
{
	if (!enabled || settings.map2d || failed) {
		return;
	}
	if (!placeShader && !Initialize()) {
		failed = true;
		return;
	}
	if (statsPending) {
		GLuint stats[4] = {};
		glGetNamedBufferSubData(statsBuffer, 0, sizeof(stats), stats);
		drawnBlades = static_cast<int>(std::min(stats[1], capacity));
		statsPending = false;
	}

	//Grass share and color of every material
	glm::vec4 materialData[TerrainMaterials::maxMaterials] = {};
	const auto& list = materials.GetMaterials();
	const int materialCount = std::min(static_cast<int>(list.size()), TerrainMaterials::maxMaterials);
	bool anyGrass = false;
	for (int m = 0; m < materialCount; m++) {
		materialData[m] = glm::vec4(list[m].grassColor, list[m].grass);
		anyGrass = anyGrass || list[m].grass > 0.0f;
	}
	const float units = vegetation::unitsPerMetre;
	const float rangeUnits = range * units;
	std::vector<ChunkWorld::GroundChunk> ground = chunks.GroundChunks(ChunkWorld::maxLevel, rangeUnits);
	ground.erase(std::remove_if(ground.begin(), ground.end(), [](const ChunkWorld::GroundChunk& g) { return !g.splat || !g.height || !g.surface; }),
		ground.end());
	if (!anyGrass || ground.empty()) {
		drawnBlades = 0;
		return;
	}

	const float modelScale = settings.modelScale;
	const glm::vec3 eye = camera.GetPosition() / modelScale;
	const Frustum frustum(camera.GetViewProjection() * glm::scale(glm::mat4(1.0f), glm::vec3(modelScale)));
	const glm::vec2 mapOrigin = chunks.GetMapOrigin();
	constexpr float wupt = TerrainView::worldUnitsPerTexel;
	const glm::vec2 eyeTexel = (glm::vec2(eye.x, eye.z) - mapOrigin) / wupt;
	const float radiusTexels = rangeUnits / wupt;
	const glm::ivec2 lookMin = glm::ivec2(glm::floor(eyeTexel - radiusTexels));
	const glm::ivec2 lookMax = glm::ivec2(glm::ceil(eyeTexel + radiusTexels)) + 1;
	const int slots = std::max(1, static_cast<int>(std::ceil(density)));

	glNamedBufferSubData(commandBuffer, 0, sizeof(emptyCommand), emptyCommand);
	placeShader->Bind();
	const GLuint program = placeShader->GetRendererID();
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bladeBuffer);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, commandBuffer);
	placeShader->SetUniform1i("heightMap", heightUnit);
	placeShader->SetUniform1i("surfaceMap", surfaceUnit);
	placeShader->SetUniform1i("splatMap", splatUnit);
	placeShader->SetUniform1i("textureSize", ChunkWorld::textureSize);
	placeShader->SetUniform1i("slots", slots);
	placeShader->SetUniform2f("mapOrigin", mapOrigin.x, mapOrigin.y);
	placeShader->SetUniform1f("worldUnitsPerTexel", wupt);
	placeShader->SetUniform1f("heightScale", settings.heightScale);
	placeShader->SetUniform3fv("eye", eye);
	glUniform4fv(glGetUniformLocation(program, "planes"), 6, &frustum.Planes()[0].x);
	glUniform1ui(glGetUniformLocation(program, "capacity"), capacity);
	placeShader->SetUniform1f("density", density);
	placeShader->SetUniform1f("range", rangeUnits);
	placeShader->SetUniform1f("bladeHeight", bladeHeight * units);
	placeShader->SetUniform1f("bladeWidth", bladeWidth * units);
	const glm::vec2 cliff = materials.CliffSlopes();
	placeShader->SetUniform2f("cliffSlopes", cliff.x, cliff.y);
	glUniform4fv(glGetUniformLocation(program, "materials"), TerrainMaterials::maxMaterials, &materialData[0].x);
	placeShader->SetUniform1i("materialCount", materialCount);
	for (const ChunkWorld::GroundChunk& g : ground) {
		const int step = 1 << g.level;
		const glm::ivec2 chunkMin = g.coord * ChunkWorld::chunkSize * step;
		const glm::ivec2 regionMin = glm::max(chunkMin, lookMin);
		const glm::ivec2 regionMax = glm::min(chunkMin + ChunkWorld::chunkSize * step, lookMax);
		const glm::ivec2 size = regionMax - regionMin;
		if (size.x <= 0 || size.y <= 0) {
			continue;
		}
		g.height->Bind(heightUnit);
		g.surface->Bind(surfaceUnit);
		g.splat->Bind(splatUnit);
		const glm::ivec2 firstTexel = chunkMin - step;
		placeShader->SetUniform2i("firstTexel", firstTexel.x, firstTexel.y);
		placeShader->SetUniform1i("step", step);
		placeShader->SetUniform2i("regionMin", regionMin.x, regionMin.y);
		placeShader->SetUniform2i("regionSize", size.x, size.y);
		const GLuint threads = static_cast<GLuint>(size.x) * static_cast<GLuint>(size.y) * static_cast<GLuint>(slots);
		glDispatchCompute((threads + 63) / 64, 1, 1);
	}
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
	glCopyNamedBufferSubData(commandBuffer, statsBuffer, 0, 0, sizeof(emptyCommand));
	statsPending = true;

	drawShader->Bind();
	frameUniforms.Upload(TerrainView::FrameBlock(camera, light, settings, glm::vec2(0.0f), glm::vec2(0.0f)));
	drawShader->SetModel(glm::scale(glm::mat4(1.0f), glm::vec3(modelScale)));
	glUniform1ui(glGetUniformLocation(drawShader->GetRendererID(), "capacity"), capacity);
	const float angle = glm::radians(windAngle);
	drawShader->SetUniform2f("windDirection", std::cos(angle), std::sin(angle));
	drawShader->SetUniform1f("windStrength", windStrength);
	drawShader->SetUniform1f("windSpeed", windSpeed);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bladeBuffer);
	const GLboolean culling = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);
	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	glBindVertexArray(vao);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, commandBuffer);
	glDrawArraysIndirect(GL_TRIANGLE_STRIP, nullptr);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	glBindVertexArray(0);
	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	if (culling) {
		glEnable(GL_CULL_FACE);
	}
}

void GrassLayer::ImGuiDraw()
{
	ImGui::SeparatorText("Grass");
	ImGui::Checkbox("Grass", &enabled);
	ImGui::SetItemTooltip("Blades of grass near the camera, on materials with grass (terrain_materials.json); needs biomes");
	if (!enabled) {
		return;
	}
	ImGui::SliderFloat("Blades per m2", &density, 1.0f, 32.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("Grass range", &range, 10.0f, 200.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
	ImGui::SetItemTooltip("Beyond a quarter of it the blades get sparser and wider; at its end they shrink away");
	ImGui::SliderFloat("Blade height", &bladeHeight, 0.1f, 1.5f, "%.2f m");
	ImGui::SliderFloat("Blade width", &bladeWidth, 0.01f, 0.2f, "%.3f m");
	ImGui::SliderFloat("Wind", &windStrength, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat("Wind speed", &windSpeed, 0.0f, 5.0f, "%.1f");
	ImGui::SliderFloat("Wind direction", &windAngle, 0.0f, 360.0f, "%.0f deg");
	ImGui::Text("Blades drawn: %d%s", drawnBlades, static_cast<GLuint>(drawnBlades) >= capacity ? " (buffer full)" : "");
}
