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

	enum Mode { MODE_NOISE = 0, MODE_LINEAR = 1, MODE_SPLINE = 2, MODE_ZERO = 3 };

	//Layout of NoiseParams in the shaders (std430)
	struct GpuNoiseParams {
		float xoffset, yoffset, scale, contrast, redistribution, lacunarity, persistence, revertGain, ridgeGain, ridgeOffset, mixPower;
		int32_t resolution, octaves, option, ridge, island, islandType, symmetrical;
	};
	static_assert(sizeof(GpuNoiseParams) == 72);

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
		float levelRanges[10];
		int32_t id;
		float r, g, b;
	};
	static_assert(sizeof(GpuBiomeEntry) == 56);

	void SetNoise(GpuNoiseParams* noises, uint32_t* permutations, int index, const noise::NoiseConfigParameters& n)
	{
		noises[index] = {
			n.xoffset, n.yoffset, n.scale, n.constrast, n.redistribution, n.lacunarity, n.persistance, n.revertGain, n.RidgeGain, n.RidgeOffset, n.mixPower,
			n.resolution, n.octaves, static_cast<int32_t>(n.option), n.Ridge ? 1 : 0, n.island ? 1 : 0, static_cast<int32_t>(n.islandType), n.symmetrical ? 1 : 0
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
	const unsigned int buffers[] = { heightInput, biomeInput, biomeLevels, biomeTable };
	glDeleteBuffers(4, buffers);
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
	//The number of levels and biomes can change, so these two get new storage on every use
	glCreateBuffers(1, &biomeLevels);
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

	int mode = MODE_ZERO;
	if (config.method == EvaluationMethod::LINEAR_COMBINE) {
		mode = MODE_LINEAR;
	}
	else if (config.method == EvaluationMethod::SPLINE_COMBINE) {
		mode = MODE_SPLINE;
		SetSpline(*input, 0, config.continentalnessSpline);
		SetSpline(*input, 1, config.mountainousnessSpline);
		SetSpline(*input, 2, config.weirdnessSpline);
	}
	glNamedBufferSubData(heightInput, 0, mode == MODE_SPLINE ? sizeof(GpuHeightInput) : offsetof(GpuHeightInput, splines), input.get());
	RunHeightShader(*heightShader, heightInput, mode, area, target);
}

void GpuHeightGenerator::GenerateBiomes(const BiomeConfig& biomes, const TerrainConfig& terrain, const std::vector<biome::Biome>& table, const MapArea& area,
	TextureClass& colors, TextureClass& ids)
{
	auto input = std::make_unique_for_overwrite<GpuBiomeInput>();
	SetNoise(input->noises, input->permutations, 0, terrain.continentalness);
	SetNoise(input->noises, input->permutations, 1, terrain.mountainousness);
	SetNoise(input->noises, input->permutations, 2, terrain.weirdness);
	SetNoise(input->noises, input->permutations, 3, biomes.temperature);
	SetNoise(input->noises, input->permutations, 4, biomes.humidity);
	glNamedBufferSubData(biomeInput, 0, sizeof(GpuBiomeInput), input.get());

	biomeShader->Bind();

	const std::vector<float>* parameterLevels[5] = { &biomes.continentalnessLevels, &biomes.mountainousnessLevels, &biomes.weirdnessLevels, &biomes.temperatureLevels, &biomes.humidityLevels };
	std::vector<float> levels;
	for (int i = 0; i < 5; i++) {
		biomeShader->SetUniform2i("levelRanges[" + std::to_string(i) + "]", static_cast<int>(levels.size()), static_cast<int>(parameterLevels[i]->size()));
		levels.insert(levels.end(), parameterLevels[i]->begin(), parameterLevels[i]->end());
	}
	glNamedBufferData(biomeLevels, levels.size() * sizeof(float), levels.data(), GL_DYNAMIC_DRAW);

	glm::vec3 fallbackColor(0.0f);
	std::vector<GpuBiomeEntry> entries;
	for (const biome::Biome& b : table) {
		const glm::vec2 c = b.GetContinentalnessLevel(), m = b.GetMountainousnessLevel(), w = b.GetWeirdnessLevel(), t = b.GetTemperatureLevel(), h = b.GetHumidityLevel();
		const glm::vec3 color = b.GetColor();
		entries.push_back({ { c.x, c.y, m.x, m.y, w.x, w.y, t.x, t.y, h.x, h.y }, b.GetId(), color.r, color.g, color.b });
		if (b.GetId() == 0) {
			fallbackColor = color;
		}
	}
	glNamedBufferData(biomeTable, entries.size() * sizeof(GpuBiomeEntry), entries.data(), GL_DYNAMIC_DRAW);

	biomeShader->SetUniform1i("biomeCount", static_cast<int>(entries.size()));
	biomeShader->SetUniform3fv("fallbackColor", fallbackColor);
	SetMapUniforms(*biomeShader, area, colors);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, biomeInput);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, biomeLevels);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, biomeTable);
	colors.BindImage(0, GL_WRITE_ONLY);
	ids.BindImage(1, GL_WRITE_ONLY);
	DispatchTiles(*biomeShader, colors.GetWidth(), colors.GetHeight());
}
