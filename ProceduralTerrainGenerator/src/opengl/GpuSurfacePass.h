#pragma once

#include <memory>

#include "glm/glm.hpp"
#include "Shader.h"
#include "TextureClass.h"

//Computes the gradient and Laplacian of a height map in a compute shader (surface_compute.shader): every texel holds
//(dx, dy, Laplacian, 0) per world unit. Only for the thread owning the GL context.
class GpuSurfacePass
{
public:
	//@return false if the shader could not be built
	bool Initialize();
	void ReloadShader() { shader->Reload(); }

	//The result can be sampled or read back right after the call
	//@param height - GL_R32F height map; must be ready for sampling (after a texture fetch barrier if a shader wrote it)
	//@param texelSize - distance between neighbouring texels in world units
	//@param target - GL_RGBA32F of the same size
	void Run(const TextureClass& height, float texelSize, TextureClass& target);
	//Run limited to the texels that depend on the heights in changed (x, y, width, height): the rectangle grown by 2 (the Laplacian at the
	//map edge reaches that far), so target must hold the surface of the heights from before the change
	void RunRegion(const TextureClass& height, float texelSize, TextureClass& target, glm::ivec4 changed);

private:
	void Dispatch(const TextureClass& height, float texelSize, TextureClass& target, glm::ivec2 origin, glm::ivec2 size);

	std::unique_ptr<Shader> shader;
};
