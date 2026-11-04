#pragma once

#include <memory>
#include <vector>

#include "Biome.h"
#include "GpuQuery.h"
#include "Shader.h"
#include "TextureClass.h"
#include "WorldConfig.h"

//Generates the maps shown by the application in compute shaders. The CPU generators do the same work and remain for
//the benchmark and for --gpu-check. Maps go to textures of any size, row y = 0 first, like HeightField. Only for the thread owning the GL context.
//The result can be sampled or read back right after a call; the work itself may still be running on the GPU.
class GpuHeightGenerator
{
public:
	GpuHeightGenerator() = default;
	~GpuHeightGenerator();

	GpuHeightGenerator(const GpuHeightGenerator&) = delete;
	GpuHeightGenerator& operator=(const GpuHeightGenerator&) = delete;

	//@return false if a shader could not be built
	bool Initialize();
	void ReloadShaders();

	//GPU version of SimplexNoiseClass::GenerateFractalNoise
	//@param target - GL_R32F
	void GenerateNoise(const noise::NoiseConfigParameters& config, float originx, float originy, TextureClass& target);
	//GPU version of TerrainGenerator::GenerateTerrain. Unlike the CPU, it does not report noise values below -1
	//as an error, and a spline takes its end value for a noise outside [-1, 1] instead of extrapolating
	//@param target - GL_R32F
	void GenerateTerrain(const TerrainConfig& config, float originx, float originy, TextureClass& target);
	//GPU version of BiomeGenerator::Biomify, including its noises. Biomes are tried in the order of the table
	//@param colors - GL_RGBA32F, color of the biome of every texel
	//@param ids - GL_R32I of the same size, id of the biome of every texel
	void GenerateBiomes(const BiomeConfig& biomes, const TerrainConfig& terrain, const std::vector<biome::Biome>& table, TextureClass& colors, TextureClass& ids);

	//Measure the GPU time of the Generate calls between them; cannot overlap another GL_TIME_ELAPSED query
	void BeginMeasure() { timer->Begin(); }
	void EndMeasure() { timer->End(); }
	//@param ms - receives the newest finished measurement, unchanged if none is ready
	void PollMeasure(double& ms);

private:
	std::unique_ptr<Shader> heightShader, biomeShader;
	unsigned int heightInput = 0, biomeInput = 0, biomeLevels = 0, biomeTable = 0;
	std::unique_ptr<GpuQuery> timer;
};
