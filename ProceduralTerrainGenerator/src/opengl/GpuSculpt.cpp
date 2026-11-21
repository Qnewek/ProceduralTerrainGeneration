#include "GpuSculpt.h"

#include <algorithm>
#include <iostream>

#include <GL/glew.h>

namespace {
	//Side of the group in sculpt_compute.shader
	constexpr int localSize = 16;
}

bool GpuSculpt::Initialize()
{
	shader = Shader::Compute("res/shaders/ComputeShaders/sculpt_compute.shader");
	if (!shader->IsValid()) {
		std::cout << "[ERROR] GpuSculpt: the compute shader could not be built" << std::endl;
		return false;
	}
	return true;
}

void GpuSculpt::ReloadShaders()
{
	shader->Reload();
}

void GpuSculpt::Apply(const sculpt::BrushConfig& config, const sculpt::Dab& dab, TextureClass& heights)
{
	const glm::ivec2 mapSize(heights.GetWidth(), heights.GetHeight());
	const glm::ivec4 area = sculpt::Area(dab, config.radius, mapSize);
	if (area.z == 0 || area.w == 0) {
		return;
	}
	//The square widened by the reach of SMOOTH, inside the map: every height the shader reads from before the dab
	const int spacing = sculpt::SmoothSpacing(config.radius);
	const int reach = config.brush == sculpt::Brush::SMOOTH ? spacing : 0;
	const glm::ivec2 copyLow = glm::max(glm::ivec2(area.x, area.y) - reach, glm::ivec2(0));
	const glm::ivec2 copyHigh = glm::min(glm::ivec2(area.x + area.z, area.y + area.w) + reach, mapSize);
	const glm::ivec2 copySize = copyHigh - copyLow;
	if (!before || before->GetWidth() < copySize.x || before->GetHeight() < copySize.y) {
		const int width = std::max(copySize.x, before ? before->GetWidth() : 0);
		const int height = std::max(copySize.y, before ? before->GetHeight() : 0);
		before = std::make_unique<TextureClass>(width, height, GL_R32F);
	}
	glCopyImageSubData(heights.GetID(), GL_TEXTURE_2D, 0, copyLow.x, copyLow.y, 0,
		before->GetID(), GL_TEXTURE_2D, 0, 0, 0, 0, copySize.x, copySize.y, 1);

	shader->Bind();
	shader->SetUniform1i("brush", static_cast<int>(config.brush));
	shader->SetUniform2i("mapSize", mapSize.x, mapSize.y);
	shader->SetUniform2i("areaOrigin", area.x, area.y);
	shader->SetUniform2i("areaSize", area.z, area.w);
	shader->SetUniform2i("copyOrigin", copyLow.x, copyLow.y);
	shader->SetUniform2f("center", dab.center.x, dab.center.y);
	shader->SetUniform1f("radius", config.radius);
	shader->SetUniform1f("hardness", config.hardness);
	shader->SetUniform1f("amount", dab.amount);
	shader->SetUniform1f("target", dab.target);
	//The shader takes the bits back as uint
	shader->SetUniform1i("seed", static_cast<int>(dab.seed));
	shader->SetUniform1f("noiseScale", config.noiseScale);
	shader->SetUniform1f("limit", dab.limit);
	shader->SetUniform1i("spacing", spacing);
	before->BindImage(0, GL_READ_ONLY);
	heights.BindImage(1, GL_WRITE_ONLY);
	glDispatchCompute((area.z + localSize - 1) / localSize, (area.w + localSize - 1) / localSize, 1);
	//Visible to sampling, to reading back and to the copy of the next dab
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
