#pragma once

#include <memory>
#include <vector>

#include <GL/glew.h>
#include "glm/glm.hpp"

#include "Hydrology.h"
#include "TextureClass.h"

//River network of a region (hydrology::Network) on the GPU: maps sampled bilinearly by common/rivers.glsl while the terrain is generated,
//and the water surface of rivers and lakes (hydrology::BuildWaterMesh) drawn by WaterSurface. Only for the thread owning the GL context.
class RiverMap
{
public:
	//Texture units the generating compute shaders read the maps from (layout(binding) in common/rivers.glsl)
	static constexpr unsigned int riverUnit = 20;
	static constexpr unsigned int levelUnit = 21;

	RiverMap() = default;
	~RiverMap();
	RiverMap(const RiverMap&) = delete;
	RiverMap& operator=(const RiverMap&) = delete;

	//Replaces the maps and the water surface; a new size gets new textures
	void Upload(const hydrology::Network& network, const std::vector<hydrology::WaterVertex>& water);
	void Clear();
	bool Ready() const { return rivers != nullptr; }

	//Binds both maps to their units
	void Bind() const;

	//Map position of cell (0, 0) (its center), map texels between cells, number of cells
	glm::vec2 Origin() const { return origin; }
	float CellSize() const { return cellSize; }
	glm::ivec2 Size() const { return rivers ? glm::ivec2(rivers->GetWidth(), rivers->GetHeight()) : glm::ivec2(0); }

	//Water surface: triangles of hydrology::WaterVertex (attributes 0 = map position, 1 = level, 2 = flow); 0 vertices = none
	GLuint WaterVertexArray() const { return waterVao; }
	GLsizei WaterVertexCount() const { return waterVertices; }

private:
	std::unique_ptr<TextureClass> rivers, levels;
	glm::vec2 origin = glm::vec2(0.0f);
	float cellSize = 1.0f;
	GLuint waterVao = 0, waterBuffer = 0;
	GLsizei waterVertices = 0;
};
