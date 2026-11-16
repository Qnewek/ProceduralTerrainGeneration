#include "GpuErosion.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
	//Size of the group in the droplet shader, and its side in the pipe shader
	constexpr int localSize = 256;
	constexpr int pipeLocalSize = 16;
	//Groups along X of one dispatch; more go to further rows
	constexpr int groupsPerRow = 32768;
	//Texture unit the source map is read from
	constexpr int sourceUnit = 0;

	//Stages of erosion_droplet_compute.shader
	enum DropletStage { STAGE_LOAD = 0, STAGE_SPAWN = 1, STAGE_STEP = 2, STAGE_STORE = 3 };
	//Size of Droplet in the shader (std430)
	constexpr GLsizeiptr dropletBytes = 8 * sizeof(float);

	//Stages of erosion_pipe_compute.shader
	enum PipeStage { PIPE_LOAD = 0, PIPE_FLUX = 1, PIPE_WATER = 2, PIPE_THERMAL_OUT = 3, PIPE_THERMAL_APPLY = 4, PIPE_STORE = 5 };

	//Evaporation during the drying after the rain (part per unit of time) and the share of water left when it ends.
	//Faster drying drops the sediment at once, on single texels (spikes): 1 left a few bumps within the talus height on test maps, 2 and more hundreds of spikes
	constexpr float dryingEvaporation = 1.0f;
	constexpr float waterLeftAfterDrying = 0.01f;

	int DryingIterations(float timeStep, float evaporation)
	{
		const float keep = 1.0f - timeStep * evaporation;
		return keep <= 0.0f ? 1 : static_cast<int>(std::ceil(std::log(waterLeftAfterDrying) / std::log(keep)));
	}
}

GpuErosion::~GpuErosion()
{
	glDeleteBuffers(1, &heightBuffer);
	glDeleteBuffers(1, &dropletBuffer);
}

bool GpuErosion::Initialize()
{
	droplet = Shader::Compute("res/shaders/ComputeShaders/erosion_droplet_compute.shader");
	pipe = Shader::Compute("res/shaders/ComputeShaders/erosion_pipe_compute.shader");
	if (!droplet->IsValid() || !pipe->IsValid()) {
		std::cout << "[ERROR] GpuErosion: the compute shaders could not be built" << std::endl;
		return false;
	}
	glCreateBuffers(1, &dropletBuffer);
	glNamedBufferStorage(dropletBuffer, dropletBatch * dropletBytes, nullptr, 0);
	for (std::unique_ptr<GpuQuery>& timer : timers) {
		timer = std::make_unique<GpuQuery>(GL_TIME_ELAPSED);
	}
	return true;
}

void GpuErosion::PollMeasure(erosion::Method method, double& ms)
{
	GLuint64 ns = 0;
	const std::unique_ptr<GpuQuery>& timer = timers[static_cast<int>(method)];
	if (timer && timer->Poll(ns)) {
		ms = static_cast<double>(ns) / 1e6;
	}
}

void GpuErosion::ReloadShaders()
{
	droplet->Reload();
	pipe->Reload();
}

void GpuErosion::ReserveHeights(size_t texels)
{
	if (texels <= heightCapacity) {
		return;
	}
	glDeleteBuffers(1, &heightBuffer);
	glCreateBuffers(1, &heightBuffer);
	glNamedBufferStorage(heightBuffer, static_cast<GLsizeiptr>(texels * sizeof(int32_t)), nullptr, 0);
	heightCapacity = texels;
}

void GpuErosion::Dispatch(int count)
{
	const int groups = (count + localSize - 1) / localSize;
	if (groups == 0) {
		return;
	}
	const int rows = (groups + groupsPerRow - 1) / groupsPerRow;
	glDispatchCompute(rows == 1 ? groups : groupsPerRow, rows, 1);
}

void GpuErosion::Erode(const erosion::ErosionConfig& config, const TextureClass& source, TextureClass& target, uint32_t seed)
{
	GpuQuery& timer = *timers[static_cast<int>(config.method)];
	timer.Begin();
	//The source may have just been written by a compute shader
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	if (config.method == erosion::Method::PIPES) {
		ErodePipes(config.pipes, source, target);
	}
	else {
		ErodeDroplets(config, source, target, seed);
	}
	//Makes the result visible to sampling in shaders and to reading the texture back
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	timer.End();
}

void GpuErosion::ErodePipes(const erosion::PipeConfig& config, const TextureClass& source, TextureClass& target)
{
	const int width = target.GetWidth(), height = target.GetHeight();
	if (!stateA || stateA->GetWidth() != width || stateA->GetHeight() != height) {
		stateA = std::make_unique<TextureClass>(width, height, GL_RGBA32F);
		stateB = std::make_unique<TextureClass>(width, height, GL_RGBA32F);
		flux = std::make_unique<TextureClass>(width, height, GL_RGBA32F);
		thermal = std::make_unique<TextureClass>(width, height, GL_RGBA32F);
	}

	pipe->Bind();
	pipe->SetUniform2i("mapSize", width, height);
	pipe->SetUniform1f("heightScale", config.heightScale);
	pipe->SetUniform1f("timeStep", config.timeStep);
	pipe->SetUniform1f("rainRate", config.rainRate);
	pipe->SetUniform1f("evaporationRate", config.evaporationRate);
	pipe->SetUniform1f("sedimentCapacity", config.sedimentCapacity);
	pipe->SetUniform1f("minTilt", config.minTilt);
	pipe->SetUniform1f("dissolvingRate", config.dissolvingRate);
	pipe->SetUniform1f("depositionRate", config.depositionRate);
	pipe->SetUniform1f("talusHeight", std::tan(std::clamp(config.talusAngle, 0.0f, 89.0f) * 3.14159265f / 180.0f));
	pipe->SetUniform1f("thermalRate", config.thermalRate);
	flux->BindImage(2, GL_READ_WRITE);
	thermal->BindImage(3, GL_READ_WRITE);

	const GLuint groupsX = (width + pipeLocalSize - 1) / pipeLocalSize, groupsY = (height + pipeLocalSize - 1) / pipeLocalSize;
	const auto run = [&](PipeStage stage) {
		pipe->SetUniform1i("stage", stage);
		glDispatchCompute(groupsX, groupsY, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	};
	//src = unit 0, dst = unit 1; they swap after every iteration
	TextureClass* src = stateA.get();
	TextureClass* dst = stateB.get();

	src->BindImage(0, GL_READ_WRITE);
	source.BindImage(4, GL_READ_ONLY);
	run(PIPE_LOAD);
	const bool thermalOn = config.thermalRate > 0.0f;
	const float dryEvaporation = std::max(config.evaporationRate, dryingEvaporation);
	const int dryIterations = config.iterations > 0 ? DryingIterations(config.timeStep, dryEvaporation) : 0;
	for (int i = 0; i < config.iterations + dryIterations; i++) {
		//The rain stops and the water dries up slowly, so the sediment settles along the streams instead of all at once at the end
		if (i == config.iterations) {
			pipe->SetUniform1f("rainRate", 0.0f);
			pipe->SetUniform1f("evaporationRate", dryEvaporation);
		}
		src->BindImage(0, GL_READ_WRITE);
		dst->BindImage(1, GL_READ_WRITE);
		run(PIPE_FLUX);
		run(PIPE_WATER);
		if (thermalOn) {
			run(PIPE_THERMAL_OUT);
			run(PIPE_THERMAL_APPLY);
		}
		std::swap(src, dst);
	}
	src->BindImage(0, GL_READ_WRITE);
	target.BindImage(4, GL_WRITE_ONLY);
	run(PIPE_STORE);
	pipe->Unbind();
}

void GpuErosion::ErodeDroplets(const erosion::ErosionConfig& config, const TextureClass& source, TextureClass& target, uint32_t seed)
{
	const int width = target.GetWidth(), height = target.GetHeight();
	const int texels = width * height;
	ReserveHeights(static_cast<size_t>(texels));

	droplet->Bind();
	droplet->SetUniform2i("mapSize", width, height);
	droplet->SetUniform1f("inertia", config.inertia);
	droplet->SetUniform1f("minSlope", config.minSlope);
	droplet->SetUniform1f("erosionRate", config.erosionRate);
	droplet->SetUniform1f("depositionRate", config.depositionRate);
	droplet->SetUniform1f("evaporationRate", config.evaporationRate);
	droplet->SetUniform1f("gravity", config.gravity);
	droplet->SetUniform1i("erosionRadius", std::max(config.erosionRadius, 0));
	droplet->SetUniform1f("blur", config.blur);
	droplet->SetUniform1f("initialVelocity", config.initialVelocity);
	droplet->SetUniform1f("initialWater", config.initialWater);
	droplet->SetUniform1i("source", sourceUnit);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, heightBuffer);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, dropletBuffer);

	droplet->SetUniform1i("stage", STAGE_LOAD);
	source.Bind(sourceUnit);
	Dispatch(texels);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	//Batches one after another, each with every droplet making step i before step i + 1 (as the CPU version did with all droplets)
	for (int first = 0, batch = 0; first < config.dropletCount; first += dropletBatch, batch++) {
		const int count = std::min(dropletBatch, config.dropletCount - first);
		droplet->SetUniform1i("dropletCount", count);
		droplet->SetUniform1i("seedBits", static_cast<int>(seed * 2654435761u + static_cast<uint32_t>(batch)));
		droplet->SetUniform1i("stage", STAGE_SPAWN);
		Dispatch(count);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		droplet->SetUniform1i("stage", STAGE_STEP);
		for (int step = 0; step < config.dropletLifetime; step++) {
			droplet->SetUniform1i("stepIndex", step);
			Dispatch(count);
			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		}
	}

	droplet->SetUniform1i("stage", STAGE_STORE);
	target.BindImage(0, GL_WRITE_ONLY);
	Dispatch(texels);
	droplet->Unbind();
}
