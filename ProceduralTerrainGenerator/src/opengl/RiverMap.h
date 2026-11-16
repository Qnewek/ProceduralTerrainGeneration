#pragma once

#include <memory>

#include "glm/glm.hpp"

#include "Hydrology.h"
#include "TextureClass.h"

//River network of a region (hydrology::Network) on the GPU, sampled bilinearly by common/rivers.glsl while the terrain is generated.
//Only for the thread owning the GL context.
class RiverMap
{
public:
	//Texture units the generating compute shaders read the maps from (layout(binding) in common/rivers.glsl)
	static constexpr unsigned int riverUnit = 20;
	static constexpr unsigned int levelUnit = 21;

	//Replaces the maps; a new size gets new textures
	void Upload(const hydrology::Network& network);
	void Clear();
	bool Ready() const { return rivers != nullptr; }

	//Binds both maps to their units
	void Bind() const;

	//Map position of cell (0, 0) (its center), map texels between cells, number of cells
	glm::vec2 Origin() const { return origin; }
	float CellSize() const { return cellSize; }
	glm::ivec2 Size() const { return rivers ? glm::ivec2(rivers->GetWidth(), rivers->GetHeight()) : glm::ivec2(0); }

	//RGBA32F: offset to the nearest course (x, y), distance, half width; RG32F: river surface, lake surface
	const TextureClass* RiverTexture() const { return rivers.get(); }
	const TextureClass* LevelTexture() const { return levels.get(); }

private:
	std::unique_ptr<TextureClass> rivers, levels;
	glm::vec2 origin = glm::vec2(0.0f);
	float cellSize = 1.0f;
};
