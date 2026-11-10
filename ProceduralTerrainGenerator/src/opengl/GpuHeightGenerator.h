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
	//@param biomes - if given and biomes->modifyTerrain, the height is changed by the biome modifiers (biome::ModifyHeight)
	void GenerateTerrain(const TerrainConfig& config, const MapArea& area, TextureClass& target, const BiomeConfig* biomes = nullptr);
	//Biome of every texel from its five noise values (biome_compute.shader): temperature and humidity changed by the terrain
	//height (biome::ApplyClimate, the height computed as GenerateTerrain does), then the nearest biome of biomes.table (biome::Nearest),
	//and colors of all biomes mixed with biome::BlendWeight. Depends on the whole terrain config, so a terrain change needs new biomes
	//@param colors - GL_RGBA32F, blended color of every texel
	//@param ids - GL_R32I of the same size, id (index in the table) of the nearest biome of every texel
	//@param splat - if given, GL_RGBA8 array of splatLayers layers of the same size: share of every surface material (biome::MixMaterials),
	//	material m in layer m / 4, channel m % 4
	void GenerateBiomes(const BiomeConfig& biomes, const TerrainConfig& terrain, const MapArea& area, TextureClass& colors, TextureClass& ids,
		TextureClass* splat = nullptr);

	//Layers of a splat map: 4 materials per RGBA layer
	static constexpr int splatLayers = biome::maxMaterials / 4;

	//Measure the GPU time of the Generate calls between them; cannot overlap another GL_TIME_ELAPSED query
	void BeginMeasure() { timer->Begin(); }
	void EndMeasure() { timer->End(); }
	//@param ms - receives the newest finished measurement, unchanged if none is ready
	void PollMeasure(double& ms);

private:
	void UploadBiomeTable(const BiomeConfig& biomes);
	//Runs height_compute.shader with the world input bound (and the biome table if modify)
	void RunHeightShader(int mode, bool modify, const MapArea& area, TextureClass& target);

	std::unique_ptr<Shader> heightShader, biomeShader;
	unsigned int worldInput = 0, biomeTable = 0;
	std::unique_ptr<GpuQuery> timer;
};
