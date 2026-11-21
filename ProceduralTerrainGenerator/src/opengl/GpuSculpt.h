#pragma once

#include <memory>

#include "Sculpt.h"
#include "Shader.h"
#include "TextureClass.h"

//Sculpt brushes on the GPU (sculpt_compute.shader): one dab changes only the square around the brush, so the cost depends on the radius,
//not on the map. Gives the same result as sculpt::Apply (checked by --gpu-check). Only for the thread owning the GL context.
class GpuSculpt
{
public:
	GpuSculpt() = default;
	GpuSculpt(const GpuSculpt&) = delete;
	GpuSculpt& operator=(const GpuSculpt&) = delete;

	//@return false if the shader could not be built
	bool Initialize();
	void ReloadShaders();

	//Applies one dab to heights in place; the result can be sampled or read back right after the call
	//@param heights - GL_R32F map in map units
	void Apply(const sculpt::BrushConfig& config, const sculpt::Dab& dab, TextureClass& heights);

private:
	std::unique_ptr<Shader> shader;
	//Heights from before the dab around the brush (the square + the reach of SMOOTH), grown when a larger one is needed
	std::unique_ptr<TextureClass> before;
};
