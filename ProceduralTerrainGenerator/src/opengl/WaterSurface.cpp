#include "WaterSurface.h"

#include <iostream>
#include <vector>

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
	//Texture unit of the chunk height map while drawing water
	constexpr int heightUnit = 11;
}

WaterSurface::WaterSurface() = default;

WaterSurface::~WaterSurface()
{
	if (vao) {
		glDeleteVertexArrays(1, &vao);
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
	const std::vector<ChunkWorld::GroundChunk> ground = chunks.GroundChunks(ChunkWorld::maxLevel, camera.GetRenderDistance() / settings.modelScale);
	if (ground.empty()) {
		return;
	}

	shader->Bind();
	//In the 2D map the same top-down camera as the terrain
	frameUniforms.Upload(TerrainView::FrameBlock(camera, light, settings, chunks.FlatViewMin(), chunks.FlatViewMax()));
	shader->SetUniform1i("flatView", settings.map2d);
	shader->SetModel(glm::scale(glm::mat4(1.0f), glm::vec3(settings.modelScale)));
	shader->SetUniform1i("heightMap", heightUnit);
	shader->SetUniform1f("heightScale", settings.heightScale);
	shader->SetUniform1f("waterLevel", waterLevel);
	shader->SetUniform1f("unitsPerMetre", vegetation::unitsPerMetre);
	shader->SetUniform1f("clarity", clarity);
	shader->SetUniform1f("waveStrength", waveStrength);
	shader->SetUniform3fv("shallowColor", shallowColor);
	shader->SetUniform3fv("deepColor", deepColor);
	//Chunk texel t is texel t + 1 of its maps, sampled at its center (as the terrain mesh)
	const float size = static_cast<float>(ChunkWorld::textureSize);
	shader->SetUniform2f("uvMin", 1.5f / size, 1.5f / size);
	shader->SetUniform2f("uvMax", (ChunkWorld::chunkSize + 1.5f) / size, (ChunkWorld::chunkSize + 1.5f) / size);

	const GLboolean culling = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	glBindVertexArray(vao);
	const glm::vec2 mapOrigin = chunks.GetMapOrigin();
	for (const ChunkWorld::GroundChunk& g : ground) {
		if (!g.height) {
			continue;
		}
		const float side = static_cast<float>(ChunkWorld::chunkSize * (1 << g.level)) * TerrainView::worldUnitsPerTexel;
		const glm::vec2 corner = mapOrigin + glm::vec2(g.coord) * side;
		g.height->Bind(heightUnit);
		shader->SetUniform2f("corner", corner.x, corner.y);
		shader->SetUniform1f("size", side);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	glBindVertexArray(0);
	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	if (culling) {
		glEnable(GL_CULL_FACE);
	}
}

void WaterSurface::ImGuiDraw()
{
	ImGui::SeparatorText("Water");
	ImGui::Checkbox("Water surface", &enabled);
	ImGui::SetItemTooltip("Sea surface at the water level of the terrain, in infinite generation (3D and 2D map)");
	if (!enabled) {
		return;
	}
	ImGui::SliderFloat("Clarity", &clarity, 0.5f, 20.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
	ImGui::SetItemTooltip("Depth through which about a third of the sea floor still shows");
	ImGui::SliderFloat("Waves", &waveStrength, 0.0f, 3.0f, "%.2f");
	ImGui::ColorEdit3("Shallow water", &shallowColor.x);
	ImGui::ColorEdit3("Deep water", &deepColor.x);
}
