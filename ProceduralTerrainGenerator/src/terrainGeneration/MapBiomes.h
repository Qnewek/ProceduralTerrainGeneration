#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "Grid.h"
#include "WorldConfig.h"

//Biomes of a map that was not generated (the "Imported map" mode): the biome table and climate of TerrainGen, with the parameters the
//noises would give computed from the map instead. Settings are saved in the map project (.ptgmap) as JSON
namespace mapbiomes {
	struct Settings {
		bool enabled = false;
		//Metres of terrain height 1 (TerrainGen heights are in [0, 1]; its default shows 1 as 256 m): the climate and the ELEVATION
		//parameter see (metres - baseMetres) / heightUnitMetres
		float heightUnitMetres = 256.0f;
		float baseMetres = 0.0f;			//metres of terrain height 0
		float coastMetres = 20000.0f;		//distance from the coast where continentalness reaches +-1
		float reliefRadiusMetres = 1000.0f;	//half the side of the square the local relief is measured in
		float reliefMetres = 600.0f;		//local relief (max - min) where mountainousness reaches 1
		glm::vec2 climateOrigin = glm::vec2(0.0f);	//noise position of texel (0, 0) in metres (TerrainGen map texels are 1 m)
		//Table, climate, temperature and humidity noises, blending and materials; the terrain modifiers are not used
		BiomeConfig config = BiomeConfig::Default();
	};

	//Defaults for a map; with a sea, height 0 lies so that the sea surface is at the water height of the default TerrainGen world
	Settings Default(std::optional<float> seaLevelMetres);
	//Settings of a map sent from another mode: the biomes of its world preset, heights as it exported them (metres = height * metresPerUnit)
	//and the noise position of its texel (0, 0); a preset that is not a world (Noise heightmap) or cannot be read gives Default
	//@param generationJson - mapproject::Project::generation
	//@param biomesOn - the mode had biomes turned on
	Settings FromGeneration(const std::string& generationJson, glm::ivec2 worldOrigin, float metresPerUnit, std::optional<float> seaLevelMetres,
		bool biomesOn);
	//Water height in terrain height units, in [0, 1]; 0 without a sea
	float WaterHeight(const Settings& settings, std::optional<float> seaLevelMetres);

	//Distance in metres from the centre of every texel to the nearest texel on the other side of the coast (land: height above the sea),
	//positive on land and negative in water; +-FLT_MAX when the map has no texel of the other kind. Exact Euclidean distance transform
	//@param pixelMetres - size of a texel along x and y
	HeightField CoastDistance(const HeightField& metres, glm::dvec2 pixelMetres, float seaLevelMetres);
	//Local relief: max - min of the heights in the rectangle of +-max(1, round(radiusMetres / pixel)) texels along each axis, cut at the map edge
	HeightField LocalRelief(const HeightField& metres, glm::dvec2 pixelMetres, float radiusMetres);

	//Continentalness and mountainousness of every texel, as GL_RG16F data (rows from y = 0)
	struct ClimateMaps {
		int width = 0, height = 0;
		std::vector<uint32_t> packed;	//glm::packHalf2x16(vec2(continentalness, mountainousness))
	};
	//Continentalness = coast distance / coastMetres and mountainousness = 2 * relief / reliefMetres - 1, both clamped to [-1, 1];
	//without a sea the continentalness is 1 everywhere. Runs on a background thread (no GL)
	ClimateMaps ComputeClimate(const HeightField& metres, glm::dvec2 pixelMetres, std::optional<float> seaLevelMetres, const Settings& settings);
	//Terrain height (TerrainGen units) the climate and the ELEVATION parameter see: (metres - baseMetres) / heightUnitMetres, except the flat
	//sea of a map of the real world (sea - 1 m <= metres <= sea), which gets WaterHeight * (1 - |continentalness|): deeper away from the coast,
	//instead of a beach everywhere. map_biome_compute.shader does the same
	float ClimateHeight(float metres, float continentalness, std::optional<float> seaLevelMetres, const Settings& settings);

	std::string ToJson(const Settings& settings);
	//@param out - replaced only on success; fields missing in the text keep their default values
	//@param error - description of the problem on failure (not JSON, a distance or height that is not above 0, a biome config presets reject)
	bool FromJson(const std::string& text, Settings& out, std::string& error);
}
