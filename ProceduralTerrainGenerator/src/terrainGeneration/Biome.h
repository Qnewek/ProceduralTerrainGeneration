#pragma once

#include <array>
#include <string>
#include <vector>

#include "glm/glm.hpp"

//Biomes as regions in the space of six parameters, as the multi-noise biomes of Minecraft 1.18: five noises and the elevation.
//Every point belongs to the nearest biome, and biomes closer than the blend width to the nearest one mix with it.
//biome_compute.shader does the same on the GPU; the functions here describe it and are used by tests.
namespace biome {
	//Parameters a biome is described by, in the order of ranges in biome_compute.shader. The first NOISE_COUNT are noises
	//(in the order of the noises of the shaders); ELEVATION is the terrain height relative to the water surface (see ApplyClimate)
	enum Parameter { CONTINENTALNESS, MOUNTAINOUSNESS, WEIRDNESS, TEMPERATURE, HUMIDITY, ELEVATION, PARAMETER_COUNT };
	constexpr int NOISE_COUNT = ELEVATION;
	//Names of the parameters for the UI, in the order of Parameter
	constexpr const char* parameterNames[PARAMETER_COUNT] = { "Continentalness", "Mountainousness", "Weirdness", "Temperature", "Humidity", "Elevation" };

	//Most biomes a table can hold (presets and the editor keep to it)
	constexpr size_t maxBiomes = 64;

	//Range of a parameter value (all are in [-1, 1]); min <= max
	struct Range {
		float min = -1.0f, max = 1.0f;
	};

	using ParameterValues = std::array<float, PARAMETER_COUNT>;

	//How a biome changes the terrain height (see ModifyHeight); the default changes nothing
	struct TerrainModifier {
		float amplitude = 1.0f;			//scales the height above (and depth below) the water surface: < 1 flattens, > 1 sharpens
		float offset = 0.0f;			//added to the height
		float detailAmplitude = 0.0f;	//height of the detail noise (dunes, rough rock); 0 = none
		float detailSize = 16.0f;		//wavelength of the detail noise in map texels; must be > 0
		bool detailRidge = false;		//sharp crests (1 - 2|n|) instead of the plain noise, e.g. dunes
	};

	//Most surface materials (layers of res/textures/terrain_materials.json; TerrainMaterials::maxMaterials)
	constexpr int maxMaterials = 16;
	//Most materials one biome mixes
	constexpr int materialLayers = 4;

	//Share of a surface material in a biome; a layer with weight 0 is unused
	struct MaterialLayer {
		int material = 0;		//index in the material list, 0 to maxMaterials - 1
		float weight = 0.0f;	//>= 0; the layers of a biome are normalized to sum 1
	};

	using MaterialWeights = std::array<float, maxMaterials>;

	struct Biome {
		std::string name;
		glm::vec3 color = glm::vec3(0.0f);
		std::array<Range, PARAMETER_COUNT> ranges;
		TerrainModifier terrain;
		//Surface materials of the biome; at least one layer has weight > 0
		std::array<MaterialLayer, materialLayers> materials = { MaterialLayer{ 0, 1.0f } };

		//Distance in the parameter space from the ranges of the biome: 0 inside all of them, otherwise
		//the Euclidean length of how far each value lies outside its range
		float Distance(const ParameterValues& values) const;
	};

	//Index of the nearest biome, the first one on ties; the table must not be empty. Biome ids are indices in the table
	int Nearest(const std::vector<Biome>& table, const ParameterValues& values);

	//Weight of a biome distance more than the nearest one: 1 at the nearest, falling smoothly to 0 at blendWidth;
	//blendWidth <= 0 = only the nearest biome
	float BlendWeight(float distance, float nearestDistance, float blendWidth);

	//How the terrain changes the climate the biomes are chosen by. Heights are terrain heights in [0, 1] (before heightScale)
	struct Climate {
		//Temperature drop per unit of height above the water surface. 0.5 (2026-11-13, was 1.0): a temperate world with snow
		//on the high mountains, not a cold one
		float lapseRate = 0.5f;
		float coastHumidity = 0.3f;		//humidity added at the water surface, fading out with height
		float coastRange = 0.05f;		//height above the water surface where the added humidity reaches 0; must be > 0
	};

	//Temperature and humidity after the climate: T - lapseRate * above, H + coastHumidity * (1 - smoothstep(0, coastRange, above)),
	//where above = max(height - waterHeight, 0); both clamped to [-1, 1], so biomes with ranges at -1 or 1 still contain them.
	//Also sets ELEVATION = Elevation(height, waterHeight). The other values are kept. biome_compute.shader does the same
	ParameterValues ApplyClimate(ParameterValues values, float height, float waterHeight, const Climate& climate);

	//Height relative to the water surface in [-1, 1]: 0 at the water surface, -1 at height 0 (the deepest sea floor), 1 at height 1;
	//linear in between on each side, so a biome range does not depend on where the water surface is
	float Elevation(float height, float waterHeight);

	//Terrain height changed by the biomes: their modifiers mixed with BlendWeight(distance, nearest, blendWidth) normalized to sum 1,
	//h = water + (height - water) * amplitude + offset + sum of weight * detailAmplitude * detail, clamped to [0, 1].
	//The biomes are chosen from the height before modifying (values after ApplyClimate), so there is no loop. blendWidth must be > 0,
	//or the height would jump at biome borders. biome_compute.shader and height_compute.shader do the same
	//@param detail - value of the detail noise of every biome at this point, in [-1, 1] (the shaders evaluate it)
	float ModifyHeight(const std::vector<Biome>& table, const ParameterValues& values, float height, float waterHeight, float blendWidth,
		const std::vector<float>& detail);

	//Multiplies the patch noise at patchiness 1. The noise (2 octaves of simplex) has a spread of about 1 / 4 of this, so
	//log(w) + noise behaves like the Gumbel trick: a material wins on about its share w of the area, in compact patches
	constexpr float patchNoiseScale = 5.0f;

	//Share of every surface material at a point (the splat map, biome_compute.shader does the same).
	//1. mix w: the material layers of every biome, each biome with BlendWeight(distance, nearest, blendWidth)
	//2. patches: score = c * (log w + k * patch), k = patchNoiseScale * patchiness, c = 1 + (contrast - 1) * patchiness;
	//	result = exp(score) normalized to sum 1 (softmax). A material leads where its score is the highest, on about its share
	//	of the area; c only sharpens the edges of the patches. Patchiness 0 gives the plain mix w.
	//blendWidth must be > 0, contrast >= 1
	//@param values - after ApplyClimate
	//@param patch - value of the patch noise of every material at this point, in [-1, 1] (the shader evaluates it)
	//@param patchiness - in [0, 1]; 0 = no patches
	MaterialWeights MixMaterials(const std::vector<Biome>& table, const ParameterValues& values, float blendWidth, float patchiness,
		float contrast, const MaterialWeights& patch);

	//Biomes used by default; materials refer to the default material list (res/textures/terrain_materials.json)
	std::vector<Biome> DefaultBiomes();
}
