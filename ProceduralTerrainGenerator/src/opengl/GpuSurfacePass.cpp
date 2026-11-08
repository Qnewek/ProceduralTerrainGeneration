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
	shader->Bind();
	shader->SetUniform1i("heightMap", heightUnit);
	shader->SetUniform1f("texelSize", texelSize);
	height.Bind(heightUnit);
	target.BindImage(0, GL_WRITE_ONLY);
	//A few texture reads per texel, so even an 8192 map is short enough for one dispatch
	glDispatchCompute((target.GetWidth() + localSize - 1) / localSize, (target.GetHeight() + localSize - 1) / localSize, 1);
	//Makes the result visible to sampling in shaders and to reading the texture back
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
	shader->Unbind();
}
