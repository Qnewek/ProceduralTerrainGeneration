#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "Biome.h"

//Biomes painted over the automatic ones of a map (Imported map, stage D2): a palette of biome names and, per texel, a slot of the palette
//and a strength. The GPU (map_biome_compute.shader) mixes the automatic biome with the painted one by the strength; these functions are
//the CPU rules of it, checked against the shader by --gpu-check (section mapbiomes)
namespace biomepaint {
	//Names of the palette; slot 0 = not painted
	inline constexpr int maxPalette = 255;

	struct Layer {
		int width = 0, height = 0;
		std::vector<std::string> palette{};	//palette[i] = biome name of slot i + 1
		std::vector<uint16_t> texels{};		//slot (low byte) | strength 0-255 (high byte): GL_RG8UI as uploaded, R = slot, G = strength
		//true when no texel has a strength above 0
		bool Empty() const;
		size_t Bytes() const { return texels.size() * sizeof(uint16_t); }
	};

	//A layer with nothing painted
	Layer Make(int width, int height);

	//@return the slot of a biome name, added at the end of the palette if new; 0 when the palette is full
	uint8_t SlotOf(Layer& layer, const std::string& name);

	//One use of the brush
	struct Dab {
		glm::vec2 center = glm::vec2(0.0f);	//map texels
		float radius = 32.0f;		//map texels
		float hardness = 0.5f;		//sculpt::Falloff
		float amount = 0.0f;		//change of the strength (0 to 1) where the falloff is 1
		uint8_t slot = 0;			//painted biome; ignored by the eraser
		bool erase = false;
		uint32_t seed = 0;			//another one for every dab of a stroke: dithers the rounding of the change
	};

	//Paints (or erases) the texels within the radius. Painting a slot over another one lowers the other one first, then raises the new one:
	//the biome goes from the old one through the automatic one to the new one
	//@return the changed rectangle (x, y, width, height); width 0 = nothing changed
	glm::ivec4 Apply(Layer& layer, const Dab& dab);

	//@return for every slot the index of the first biome of the table with its name; -1 for slot 0 and for names not in the table
	std::array<int, 256> SlotBiomes(const Layer& layer, const std::vector<biome::Biome>& table);

	//Materials of a painted texel before the patches: the automatic mix and the layers of the painted biome, each normalized to sum 1,
	//mixed by the strength (0 to 1)
	biome::MaterialWeights PaintedMaterials(const biome::MaterialWeights& automatic, const biome::Biome& painted, float strength);
}
