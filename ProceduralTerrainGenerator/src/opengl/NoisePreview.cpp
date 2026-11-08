#include "NoisePreview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "imgui.h"

namespace {
	//Size of the group in the shader
	constexpr int localSize = 16;
	//Texture unit the values are read from during the pass
	constexpr int valueUnit = 0;
}

bool NoisePreview::Initialize()
{
	shader = Shader::Compute("res/shaders/ComputeShaders/preview_compute.shader");
	if (!shader->IsValid()) {
		std::cout << "[ERROR] NoisePreview: the compute shader could not be built" << std::endl;
		return false;
	}
	return true;
}

void NoisePreview::Generate(GpuHeightGenerator& gpu, const noise::NoiseConfigParameters& config, int mapWidth, int mapHeight)
{
	//Larger maps are sampled more sparsely, so the picture covers the whole map in at most maxSize texels
	const float step = std::max(1.0f, static_cast<float>(std::max(mapWidth, mapHeight)) / maxSize);
	const int width = std::max(1, static_cast<int>(std::ceil(mapWidth / step)));
	const int height = std::max(1, static_cast<int>(std::ceil(mapHeight / step)));
	if (!values || values->GetWidth() != width || values->GetHeight() != height) {
		values = std::make_unique<TextureClass>(width, height, GL_R32F);
		colors = std::make_unique<TextureClass>(width, height, GL_RGBA8);
	}

	MapArea area;
	area.step = step;
	area.islandSize = glm::ivec2(mapWidth, mapHeight);
	gpu.GenerateNoise(config, area, *values);

	shader->Bind();
	shader->SetUniform1i("values", valueUnit);
	values->Bind(valueUnit);
	colors->BindImage(0, GL_WRITE_ONLY);
	glDispatchCompute((width + localSize - 1) / localSize, (height + localSize - 1) / localSize, 1);
	//ImGui samples the result when it draws
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
	shader->Unbind();
}

void NoisePreview::ImGuiDraw() const
{
	if (!colors) {
		return;
	}
	const float width = ImGui::GetContentRegionAvail().x;
	const float height = width * colors->GetHeight() / colors->GetWidth();
	ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(colors->GetID())), ImVec2(width, height));
	ImGui::TextDisabled("black = -1, gray = 0, white = 1");
}
