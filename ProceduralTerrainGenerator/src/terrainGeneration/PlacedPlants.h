#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "Grid.h"
#include "VegetationExport.h"

//Plants put on the Imported map by hand (the plant brush of the Vegetation tab): kept apart from the automatic plants, which are placed
//again after every change of the biomes, so a biome change never removes them
namespace placedplants {
	struct Plant {
		glm::vec2 position = glm::vec2(0.0f);	//metres from the centre of texel (0, 0): x along the columns, y along the rows
		float yaw = 0.0f;						//radians around +Y
		float scale = 1.0f;						//metres per metre of the model
		uint16_t model = 0;						//index into Layer::palette
		uint8_t variant = 0;					//taken modulo the variants the model has when drawn

		bool operator==(const Plant&) const = default;
	};

	struct Layer {
		std::vector<std::string> palette;		//model names; a name no longer in the model list is kept and not drawn
		std::vector<Plant> plants;

		size_t Bytes() const;
		bool operator==(const Layer&) const = default;
	};

	inline constexpr int maxPalette = 65535;
	//Pack: x, z, yaw, scale as float32, model uint16, variant uint8, little-endian
	inline constexpr size_t plantBytes = 19;

	struct Brush {
		glm::vec2 centre = glm::vec2(0.0f);	//metres, as Plant::position
		float radius = 10.0f;				//metres; the disc the plants of a dab fall in
		int count = 1;						//1 = one plant exactly at the centre; more = candidates spread over the disc
		float spacing = 2.0f;				//metres; no plant closer than this to another of the layer
		float scaleMin = 0.8f, scaleMax = 1.2f;
		std::vector<std::string> models;	//chosen uniformly for each plant
		std::vector<int> variants;			//variants of each model (at least 1)
		uint32_t seed = 0;					//the same layer, brush and seed give the same plants
	};

	//Adds the plants of one dab; candidates off the map ([0, extent]) or closer than spacing to a plant of the layer are dropped
	//@return plants added; 0 without models or when the palette is full
	size_t Scatter(Layer& layer, const Brush& brush, glm::vec2 extent);
	//Removes the plants within radius of centre (order of the others kept)
	//@return plants removed
	size_t Erase(Layer& layer, glm::vec2 centre, float radius);
	//Plants to draw or export, standing on the map: names looked up in models (unknown or not loaded = skipped), plants off the map
	//([0, (W-1) p] x [0, (H-1) p]) skipped, y = bilinear height of units times metresPerUnit
	std::vector<vegexport::Placed> ToPlaced(const Layer& layer, const HeightField& units, float metresPerUnit, float metresPerTexel,
		const std::vector<vegexport::Model>& models);

	std::vector<uint8_t> Pack(const Layer& layer);
	//Reads the plants of a block of Pack into layer.plants, checked against layer.palette (set before); on failure layer is unchanged
	bool Unpack(const uint8_t* data, size_t size, Layer& layer, std::string& error);
}
