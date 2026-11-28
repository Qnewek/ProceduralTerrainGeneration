#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

#include "GpuQuery.h"
#include "Shader.h"
#include "TextureClass.h"
#include "WorldConfig.h"

//Hydraulic erosion of a height map in compute shaders, by the method chosen in ErosionConfig. Only for the thread owning the GL context.
//Droplets: particle hydraulic erosion, erosion_droplet_compute.shader.
//Pipes: water on a grid (virtual pipes) + thermal erosion, erosion_pipe_compute.shader; deterministic.
class GpuErosion
{
public:
	GpuErosion() = default;
	~GpuErosion();
	GpuErosion(const GpuErosion&) = delete;
	GpuErosion& operator=(const GpuErosion&) = delete;

	//@return false if a shader could not be built
	bool Initialize();
	void ReloadShaders();

	//Erodes source into target; the result can be sampled or read back right after the call
	//@param source - GL_R32F map to start from; may be target itself, so that erosion accumulates
	//@param target - GL_R32F of the same size
	//@param seed - start positions of the droplets; give every run its own, or runs that accumulate repeat the same droplets. Pipes ignore it
	void Erode(const erosion::ErosionConfig& config, const TextureClass& source, TextureClass& target, uint32_t seed);

	//GPU time of the last Erode with the method, once the GPU has it; Erode cannot run inside another GL_TIME_ELAPSED query
	//@param ms - left unchanged if no new result is ready
	void PollMeasure(erosion::Method method, double& ms);

	//Droplets simulated together; more are run in batches one after another
	static constexpr int dropletBatch = 1 << 18;

private:
	std::unique_ptr<Shader> droplet, pipe;
	//One per method (index = erosion::Method), so both can be measured in one frame
	std::unique_ptr<GpuQuery> timers[2];
	GLuint heightBuffer = 0, dropletBuffer = 0;
	//Texels the height buffer holds
	size_t heightCapacity = 0;
	//State of the pipe model (see erosion_pipe_compute.shader), the size of the last map
	std::unique_ptr<TextureClass> stateA, stateB, flux, thermal;

	void ErodeDroplets(const erosion::ErosionConfig& config, const TextureClass& source, TextureClass& target, uint32_t seed);
	void ErodePipes(const erosion::PipeConfig& config, const TextureClass& source, TextureClass& target);

	//Height buffer with room for the map
	void ReserveHeights(size_t texels);
	//Runs one droplet stage over count items (texels or droplets)
	void Dispatch(int count);
};
