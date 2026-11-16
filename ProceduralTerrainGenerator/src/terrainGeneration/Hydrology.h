#pragma once

#include <vector>

#include "glm/glm.hpp"

#include "Grid.h"
#include "WorldConfig.h"

//River network of a region of the world (rivers::Method::REGION), computed on the CPU from a coarse sample of the terrain without rivers
//(GpuHeightGenerator::SampleTerrain). Water drains from every cell (priority flood, Barnes et al. 2014), depressions fill into lakes,
//a river starts where enough area drains through a cell and widens downstream. The courses are smoothed polylines; the GPU samples
//the distance to them per cell (common/rivers.glsl) and carves the channels.
namespace hydrology {
	//Distance of a cell no river course comes near (texels); bilinear sampling next to it gives large distances, never a false channel
	constexpr float farDistance = 1.0e5f;
	//Lake surface of a cell outside lakes
	constexpr float noLake = -1.0f;

	//Per cell, sampled bilinearly by the GPU: offset from the cell center to the nearest point of a river course (map texels; exact
	//under bilinear sampling along a straight course), its length (robust where the nearest river changes) and the half width there
	struct RiverTexel {
		float offsetX = 0.0f, offsetY = 0.0f;
		float distance = farDistance;
		float halfWidth = 0.0f;
	};
	//Water surface of the nearest river, and of the lake the cell lies in (noLake if none)
	struct LevelTexel {
		float river = 0.0f;
		float lake = noLake;
	};

	//How water flows over a height map
	struct Drainage {
		//Depressions filled and rising slightly towards the outlets, so water drains from every cell
		HeightField filled;
		//Depressions filled flat: the surface of a lake where it lies above the terrain
		HeightField lakeLevel;
		//Cell the water flows to (steepest descent on filled, 8 neighbours), -1 at outlets: the border and cells under water
		std::vector<int> receiver;
		//All cells, each after its receiver
		std::vector<int> order;
		//Cells draining through a cell, itself included
		std::vector<float> area;
	};

	//@param waterHeight - cells below it are sea (outlets)
	Drainage Drain(const HeightField& terrain, float waterHeight);

	struct Network {
		//Cell (x, y) covers map position origin + (x, y) * cellSize (its center)
		glm::vec2 origin = glm::vec2(0.0f);
		float cellSize = 1.0f;
		Grid<RiverTexel> rivers;
		Grid<LevelTexel> levels;
		//Statistics for the UI
		int riverCells = 0, lakeCells = 0, lakes = 0;
	};

	//@param terrain - heights at the cell centers, without rivers
	//@param origin - map position of cell (0, 0)
	//@param cellSize - map texels between cells
	//@param waterHeight - sea level (TerrainConfig::WaterHeight)
	Network Compute(const HeightField& terrain, glm::vec2 origin, float cellSize, float waterHeight, const rivers::RiverConfig& config);
}
