#include "GpuHeightGenerator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

#include "Simplex/SimplexNoise.h"
#include "SplineSampling.h"

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

	//Layout of the WorldInput block of both shaders (common/world_input.glsl, std430): noises in the order of biome::Parameter,
	//permutation table 5 for the detail noise of the biome modifiers, the three terrain splines
	constexpr int detailTable = 5;
	struct GpuWorldInput {
		GpuNoiseParams noises[5];
		uint32_t permutations[6 * 256];
		float splines[3 * splineSamples];
	};

	//Layout of BiomeEntry in common/biomes.glsl (std430)
	struct GpuBiomeEntry {
		float ranges[2 * biome::PARAMETER_COUNT];
		float r, g, b;
		float amplitude, offset, detailAmplitude, detailSize;
		int32_t detailRidge;
	};
	static_assert(sizeof(GpuBiomeEntry) == 72);

	void SetPermutation(uint32_t* permutations, int index, int seed)
	{
		std::array<uint8_t, 256> perm;
		SimplexNoise::makePermutation(seed, perm.data());
		std::copy(perm.begin(), perm.end(), permutations + index * 256);
	}

	void SetNoise(GpuNoiseParams* noises, uint32_t* permutations, int index, const noise::NoiseConfigParameters& n)
	{
		noises[index] = {
			n.xoffset, n.yoffset, n.scale, n.constrast, n.redistribution, n.lacunarity, n.persistance, n.revertGain, n.RidgeGain, n.RidgeOffset, n.mixPower,
			n.warpStrength, n.warpScale, n.slopeErosion, n.terraceSharpness,
			n.resolution, n.octaves, static_cast<int32_t>(n.option), n.Ridge ? 1 : 0, n.island ? 1 : 0, static_cast<int32_t>(n.islandType), n.symmetrical ? 1 : 0,
			n.terraceSteps
		};
		SetPermutation(permutations, index, n.seed);
	}

	//Fills the splines the terrain method uses (none for LINEAR_COMBINE)
	//@return the mode of terrain.glsl for the method
	int SetTerrainSplines(float* splines, const TerrainConfig& config)
	{
		if (config.method == EvaluationMethod::LINEAR_COMBINE) {
			return MODE_LINEAR;
		}
		const WorldGenParameter components[] = { WorldGenParameter::CONTINENTALNESS, WorldGenParameter::MOUNTAINOUSNESS, WorldGenParameter::WEIRDNESS };
		for (int i = 0; i < 3; i++) {
			const std::vector<float> samples = SampleSpline(config.MethodSpline(components[i]), splineSamples);
			std::copy(samples.begin(), samples.end(), splines + i * splineSamples);
		}
		return config.method == EvaluationMethod::LAYERED ? MODE_LAYERED : MODE_SPLINE;
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

	//Fills the noises, permutation tables and splines of the terrain and, if given, of the biomes
	//@return the mode of terrain.glsl for the terrain method
	int FillWorldInput(GpuWorldInput& input, const TerrainConfig& terrain, const BiomeConfig* biomes)
	{
		SetNoise(input.noises, input.permutations, 0, terrain.continentalness);
		SetNoise(input.noises, input.permutations, 1, terrain.mountainousness);
		SetNoise(input.noises, input.permutations, 2, terrain.weirdness);
		if (biomes) {
			SetNoise(input.noises, input.permutations, 3, biomes->temperature);
			SetNoise(input.noises, input.permutations, 4, biomes->humidity);
			SetPermutation(input.permutations, detailTable, biomes->detailSeed);
		}
		return SetTerrainSplines(input.splines, terrain);
	}

	//Sets the uniforms of the terrain method and of common/biomes.glsl; the shader must be bound
	void SetWorldUniforms(Shader& shader, int mode, const TerrainConfig& terrain, const BiomeConfig& biomes)
	{
		shader.SetUniform1i("mode", mode);
		shader.SetUniform3fv("layered", glm::vec3(terrain.seaLevel, terrain.coastWidth, terrain.mountainShare));
		shader.SetUniform1i("biomeCount", static_cast<int>(biomes.table.size()));
		shader.SetUniform1f("waterHeight", terrain.WaterHeight());
		shader.SetUniform3fv("climate", glm::vec3(biomes.climate.lapseRate, biomes.climate.coastHumidity, biomes.climate.coastRange));
	}
}

void GpuHeightGenerator::UploadBiomeTable(const BiomeConfig& biomes)
{
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
		e.amplitude = b.terrain.amplitude;
		e.offset = b.terrain.offset;
		e.detailAmplitude = b.terrain.detailAmplitude;
		e.detailSize = b.terrain.detailSize;
		e.detailRidge = b.terrain.detailRidge ? 1 : 0;
	}
	//The number of biomes can change, so the buffer gets new storage on every use
	glNamedBufferData(biomeTable, entries.size() * sizeof(GpuBiomeEntry), entries.data(), GL_DYNAMIC_DRAW);
}

void GpuHeightGenerator::RunHeightShader(int mode, bool modify, const MapArea& area, TextureClass& target)
{
	Shader& shader = *heightShader;
	shader.Bind();
	shader.SetUniform1i("mode", mode);
	shader.SetUniform1i("modifyTerrain", modify ? 1 : 0);
	SetMapUniforms(shader, area, target);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, worldInput);
	if (modify) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, biomeTable);
	}
	target.BindImage(0, GL_WRITE_ONLY);
	DispatchTiles(shader, target.GetWidth(), target.GetHeight());
}

GpuHeightGenerator::~GpuHeightGenerator()
{
	const unsigned int buffers[] = { worldInput, biomeTable };
	glDeleteBuffers(2, buffers);
}

bool GpuHeightGenerator::Initialize()
{
	heightShader = Shader::Compute("res/shaders/ComputeShaders/height_compute.shader");
	biomeShader = Shader::Compute("res/shaders/ComputeShaders/biome_compute.shader");
	if (!heightShader->IsValid() || !biomeShader->IsValid()) {
		std::cout << "[ERROR] GpuHeightGenerator: a compute shader could not be built" << std::endl;
		return false;
	}
	//One input buffer for both shaders; updating it after a dispatch waits for the dispatch (GL orders buffer updates)
	glCreateBuffers(1, &worldInput);
	glNamedBufferStorage(worldInput, sizeof(GpuWorldInput), nullptr, GL_DYNAMIC_STORAGE_BIT);
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
	auto input = std::make_unique_for_overwrite<GpuWorldInput>();
	SetNoise(input->noises, input->permutations, 0, config);
	glNamedBufferSubData(worldInput, 0, sizeof(GpuWorldInput), input.get());
	RunHeightShader(MODE_NOISE, false, area, target);
}

void GpuHeightGenerator::GenerateTerrain(const TerrainConfig& config, const MapArea& area, TextureClass& target, const BiomeConfig* biomes)
{
	auto input = std::make_unique_for_overwrite<GpuWorldInput>();
	const bool modify = biomes && biomes->modifyTerrain;
	const int mode = FillWorldInput(*input, config, modify ? biomes : nullptr);
	glNamedBufferSubData(worldInput, 0, sizeof(GpuWorldInput), input.get());
	heightShader->Bind();
	heightShader->SetUniform3fv("layered", glm::vec3(config.seaLevel, config.coastWidth, config.mountainShare));
	if (modify) {
		UploadBiomeTable(*biomes);
		SetWorldUniforms(*heightShader, mode, config, *biomes);
		//Only the height shader uses it
		heightShader->SetUniform1f("terrainBlendWidth", biomes->terrainBlendWidth);
	}
	RunHeightShader(mode, modify, area, target);
}

void GpuHeightGenerator::GenerateBiomes(const BiomeConfig& biomes, const TerrainConfig& terrain, const MapArea& area, TextureClass& colors, TextureClass& ids)
{
	auto input = std::make_unique_for_overwrite<GpuWorldInput>();
	const int mode = FillWorldInput(*input, terrain, &biomes);
	glNamedBufferSubData(worldInput, 0, sizeof(GpuWorldInput), input.get());
	UploadBiomeTable(biomes);

	biomeShader->Bind();
	SetWorldUniforms(*biomeShader, mode, terrain, biomes);
	biomeShader->SetUniform1f("blendWidth", biomes.blendWidth);
	SetMapUniforms(*biomeShader, area, colors);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, worldInput);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, biomeTable);
	colors.BindImage(0, GL_WRITE_ONLY);
	ids.BindImage(1, GL_WRITE_ONLY);
	DispatchTiles(*biomeShader, colors.GetWidth(), colors.GetHeight());
}
