#include "GpuHeightGenerator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

#include "Simplex/SimplexNoise.h"
#include "Splines/spline.h"

namespace {
	constexpr int splineSamples = 1024;
	//Size of the group in the shaders
	constexpr int localSize = 32;
	//A long dispatch can trigger the Windows GPU watchdog and freezes the desktop, so large maps go in parts
	constexpr int tileSize = 1024;

	enum Mode { MODE_NOISE = 0, MODE_LINEAR = 1, MODE_SPLINE = 2, MODE_LAYERED = 3 };

	//Layout of NoiseParams in the shaders (std430)
	struct GpuNoiseParams {
		float xoffset, yoffset, scale, contrast, redistribution, lacunarity, persistence, revertGain, ridgeGain, ridgeOffset, mixPower;
		float warpStrength, warpScale, slopeErosion, terraceSharpness;
		int32_t resolution, octaves, option, ridge, island, islandType, symmetrical, terraceSteps;
	};
	static_assert(sizeof(GpuNoiseParams) == 92);

	//Layout of the HeightInput block in the height shader (std430)
	struct GpuHeightInput {
		GpuNoiseParams noises[3];
		uint32_t permutations[3 * 256];
		float splines[3 * splineSamples];
	};

	//Layout of the BiomeInput block in the biome shader (std430)
	struct GpuBiomeInput {
		GpuNoiseParams noises[5];
		uint32_t permutations[5 * 256];
	};

	//Layout of BiomeEntry in the biome shader (std430)
	struct GpuBiomeEntry {
		float ranges[2 * biome::PARAMETER_COUNT];
		float r, g, b;
	};
	static_assert(sizeof(GpuBiomeEntry) == 52);

	void SetNoise(GpuNoiseParams* noises, uint32_t* permutations, int index, const noise::NoiseConfigParameters& n)
	{
		noises[index] = {
			n.xoffset, n.yoffset, n.scale, n.constrast, n.redistribution, n.lacunarity, n.persistance, n.revertGain, n.RidgeGain, n.RidgeOffset, n.mixPower,
			n.warpStrength, n.warpScale, n.slopeErosion, n.terraceSharpness,
			n.resolution, n.octaves, static_cast<int32_t>(n.option), n.Ridge ? 1 : 0, n.island ? 1 : 0, static_cast<int32_t>(n.islandType), n.symmetrical ? 1 : 0,
			n.terraceSteps
		};

		std::array<uint8_t, 256> perm;
		SimplexNoise::makePermutation(n.seed, perm.data());
		std::copy(perm.begin(), perm.end(), permutations + index * 256);
	}

	void SetSpline(GpuHeightInput& input, int index, const SplinePoints& points)
	{
		tk::spline spline;
		spline.set_points(points.x, points.y, points.cubic ? tk::spline::cspline : tk::spline::linear);
		for (int i = 0; i < splineSamples; i++) {
			const double x = -1.0 + 2.0 * i / (splineSamples - 1);
			input.splines[index * splineSamples + i] = static_cast<float>(spline(x));
		}
	}

	//Runs the bound shader over the whole map in parts; it reads the part from the tileOrigin uniform
	void DispatchTiles(Shader& shader, int width, int height)
	{
		const unsigned int groups = (tileSize + localSize - 1) / localSize;
		for (int y = 0; y < height; y += tileSize) {
			for (int x = 0; x < width; x += tileSize) {
				shader.SetUniform2i("tileOrigin", x, y);
				glDispatchCompute(groups, groups, 1);
				//Lets the driver submit each part separately
				glFlush();
			}
		}
		//Makes the result visible to sampling in shaders and to reading the texture back
		glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
		shader.Unbind();
	}

	//Sets the uniforms shared by the generating shaders; the shader must be bound
	void SetMapUniforms(Shader& shader, const MapArea& area, const TextureClass& target)
	{
		glm::ivec2 islandSize = area.islandSize;
		if (islandSize.x <= 0 || islandSize.y <= 0) {
			islandSize = glm::ivec2(target.GetWidth(), target.GetHeight());
		}
		shader.SetUniform2i("mapSize", target.GetWidth(), target.GetHeight());
		shader.SetUniform2f("origin", area.origin.x, area.origin.y);
		shader.SetUniform1f("sampleStep", area.step);
		shader.SetUniform2i("islandSize", islandSize.x, islandSize.y);
	}

	void RunHeightShader(Shader& shader, unsigned int input, int mode, const MapArea& area, TextureClass& target)
	{
		shader.Bind();
		shader.SetUniform1i("mode", mode);
		SetMapUniforms(shader, area, target);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input);
		target.BindImage(0, GL_WRITE_ONLY);
		DispatchTiles(shader, target.GetWidth(), target.GetHeight());
	}
}

GpuHeightGenerator::~GpuHeightGenerator()
{
	const unsigned int buffers[] = { heightInput, biomeInput, biomeTable };
	glDeleteBuffers(3, buffers);
}

bool GpuHeightGenerator::Initialize()
{
	heightShader = Shader::Compute("res/shaders/ComputeShaders/height_compute.shader");
	biomeShader = Shader::Compute("res/shaders/ComputeShaders/biome_compute.shader");
	if (!heightShader->IsValid() || !biomeShader->IsValid()) {
		std::cout << "[ERROR] GpuHeightGenerator: a compute shader could not be built" << std::endl;
		return false;
	}
	glCreateBuffers(1, &heightInput);
	glNamedBufferStorage(heightInput, sizeof(GpuHeightInput), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glCreateBuffers(1, &biomeInput);
	glNamedBufferStorage(biomeInput, sizeof(GpuBiomeInput), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glCreateBuffers(1, &biomeTable);
	timer = std::make_unique<GpuQuery>(GL_TIME_ELAPSED);
	return true;
}

void GpuHeightGenerator::ReloadShaders()
{
	heightShader->Reload();
	biomeShader->Reload();
}

void GpuHeightGenerator::PollMeasure(double& ms)
{
	GLuint64 nanoseconds = 0;
	if (timer->Poll(nanoseconds)) {
		ms = static_cast<double>(nanoseconds) / 1e6;
	}
}

void GpuHeightGenerator::GenerateNoise(const noise::NoiseConfigParameters& config, const MapArea& area, TextureClass& target)
{
	//Uninitialized on purpose: the shader reads only what is set for the mode
	auto input = std::make_unique_for_overwrite<GpuHeightInput>();
	SetNoise(input->noises, input->permutations, 0, config);
	glNamedBufferSubData(heightInput, 0, offsetof(GpuHeightInput, splines), input.get());
	RunHeightShader(*heightShader, heightInput, MODE_NOISE, area, target);
}

void GpuHeightGenerator::GenerateTerrain(const TerrainConfig& config, const MapArea& area, TextureClass& target)
{
	auto input = std::make_unique_for_overwrite<GpuHeightInput>();
	SetNoise(input->noises, input->permutations, 0, config.continentalness);
	SetNoise(input->noises, input->permutations, 1, config.mountainousness);
	SetNoise(input->noises, input->permutations, 2, config.weirdness);

	int mode = MODE_LINEAR;
	if (config.method != EvaluationMethod::LINEAR_COMBINE) {
		mode = config.method == EvaluationMethod::LAYERED ? MODE_LAYERED : MODE_SPLINE;
		SetSpline(*input, 0, config.MethodSpline(WorldGenParameter::CONTINENTALNESS));
		SetSpline(*input, 1, config.MethodSpline(WorldGenParameter::MOUNTAINOUSNESS));
		SetSpline(*input, 2, config.MethodSpline(WorldGenParameter::WEIRDNESS));
	}
	glNamedBufferSubData(heightInput, 0, mode == MODE_LINEAR ? offsetof(GpuHeightInput, splines) : sizeof(GpuHeightInput), input.get());
	heightShader->Bind();
	heightShader->SetUniform3fv("layered", glm::vec3(config.seaLevel, config.coastWidth, config.mountainShare));
	RunHeightShader(*heightShader, heightInput, mode, area, target);
}

void GpuHeightGenerator::GenerateBiomes(const BiomeConfig& biomes, const TerrainConfig& terrain, const MapArea& area, TextureClass& colors, TextureClass& ids)
{
	//The noises in the order of biome::Parameter
	auto input = std::make_unique_for_overwrite<GpuBiomeInput>();
	SetNoise(input->noises, input->permutations, 0, terrain.continentalness);
	SetNoise(input->noises, input->permutations, 1, terrain.mountainousness);
	SetNoise(input->noises, input->permutations, 2, terrain.weirdness);
	SetNoise(input->noises, input->permutations, 3, biomes.temperature);
	SetNoise(input->noises, input->permutations, 4, biomes.humidity);
	glNamedBufferSubData(biomeInput, 0, sizeof(GpuBiomeInput), input.get());

	std::vector<GpuBiomeEntry> entries;
	for (const biome::Biome& b : biomes.table) {
		GpuBiomeEntry& e = entries.emplace_back();
		for (int i = 0; i < biome::PARAMETER_COUNT; i++) {
			e.ranges[i * 2] = b.ranges[i].min;
			e.ranges[i * 2 + 1] = b.ranges[i].max;
		}
		e.r = b.color.r;
		e.g = b.color.g;
		e.b = b.color.b;
	}
	//The number of biomes can change, so the buffer gets new storage on every use
	glNamedBufferData(biomeTable, entries.size() * sizeof(GpuBiomeEntry), entries.data(), GL_DYNAMIC_DRAW);

	biomeShader->Bind();
	biomeShader->SetUniform1i("biomeCount", static_cast<int>(entries.size()));
	biomeShader->SetUniform1f("blendWidth", biomes.blendWidth);
	SetMapUniforms(*biomeShader, area, colors);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, biomeInput);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, biomeTable);
	colors.BindImage(0, GL_WRITE_ONLY);
	ids.BindImage(1, GL_WRITE_ONLY);
	DispatchTiles(*biomeShader, colors.GetWidth(), colors.GetHeight());
}
