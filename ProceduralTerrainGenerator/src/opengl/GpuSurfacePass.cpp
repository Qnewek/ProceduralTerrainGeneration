#include "GpuSurfacePass.h"

#include <iostream>

namespace {
	//Size of the group in the shader
	constexpr int localSize = 16;
	//Texture unit the height map is read from during the pass
	constexpr int heightUnit = 0;
}

bool GpuSurfacePass::Initialize()
{
	shader = Shader::Compute("res/shaders/ComputeShaders/surface_compute.shader");
	if (!shader->IsValid()) {
		std::cout << "[ERROR] GpuSurfacePass: the compute shader could not be built" << std::endl;
		return false;
	}
	return true;
}

void GpuSurfacePass::Run(const TextureClass& height, float texelSize, TextureClass& target)
{
	Dispatch(height, texelSize, target, glm::ivec2(0), glm::ivec2(target.GetWidth(), target.GetHeight()));
}

void GpuSurfacePass::RunRegion(const TextureClass& height, float texelSize, TextureClass& target, glm::ivec4 changed)
{
	constexpr int reach = 2;
	const glm::ivec2 low = glm::max(glm::ivec2(changed.x, changed.y) - reach, glm::ivec2(0));
	const glm::ivec2 high = glm::min(glm::ivec2(changed.x + changed.z, changed.y + changed.w) + reach, glm::ivec2(target.GetWidth(), target.GetHeight()));
	if (changed.z > 0 && changed.w > 0 && high.x > low.x && high.y > low.y) {
		Dispatch(height, texelSize, target, low, high - low);
	}
}

void GpuSurfacePass::Dispatch(const TextureClass& height, float texelSize, TextureClass& target, glm::ivec2 origin, glm::ivec2 size)
{
	shader->Bind();
	shader->SetUniform1i("heightMap", heightUnit);
	shader->SetUniform1f("texelSize", texelSize);
	shader->SetUniform2i("regionOrigin", origin.x, origin.y);
	shader->SetUniform2i("regionSize", size.x, size.y);
	height.Bind(heightUnit);
	target.BindImage(0, GL_WRITE_ONLY);
	//A few texture reads per texel, so even an 8192 map is short enough for one dispatch
	glDispatchCompute((size.x + localSize - 1) / localSize, (size.y + localSize - 1) / localSize, 1);
	//Makes the result visible to sampling in shaders and to reading the texture back
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
	shader->Unbind();
}
