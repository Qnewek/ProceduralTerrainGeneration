#pragma once

#include <memory>
#include <vector>

#include "Biome.h"
#include "GpuQuery.h"
#include "Shader.h"
#include "TextureClass.h"
#include "WorldConfig.h"

//Part of the map a target texture covers: texel (x, y) is map position origin + (x, y) * step
struct MapArea {
	glm::vec2 origin = glm::vec2(0.0f);
	//Map distance between neighbouring texels. An integer keeps samples on integer positions, so a coarser area
	//(power of two) has exactly the samples of the finer ones where they meet; 1 = as on the CPU
	float step = 1.0f;
	//Size of the map an island fills, starting at map position (0, 0); (0, 0) = the size of the target, as on the CPU
	glm::ivec2 islandSize = glm::ivec2(0);
};

//Generates all maps of the application in compute shaders (the only implementation of the generation; --gpu-check tests it).
//Maps go to textures of any size, row y = 0 first, like HeightField. Only for the thread owning the GL context.
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

	//Fractal simplex noise (height_compute.shader, common/noise.glsl)
	//@param target - GL_R32F
	void GenerateNoise(const noise::NoiseConfigParameters& config, const MapArea& area, TextureClass& target);
	//Terrain height from the three terrain noises by config.method. A spline is sampled into a table of 1024 values
	//and takes its end value for a noise outside [-1, 1]
	//@param target - GL_R32F
	void GenerateTerrain(const TerrainConfig& config, const MapArea& area, TextureClass& target);
	//Biome of every texel from the levels of its five noises (biome_compute.shader). Biomes are tried in the order of the table,
	//id 0 where none matches
	//@param colors - GL_RGBA32F, color of the biome of every texel
	//@param ids - GL_R32I of the same size, id of the biome of every texel
	void GenerateBiomes(const BiomeConfig& biomes, const TerrainConfig& terrain, const std::vector<biome::Biome>& table, const MapArea& area,
		TextureClass& colors, TextureClass& ids);

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
