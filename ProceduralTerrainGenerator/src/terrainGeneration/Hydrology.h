#pragma once

#include <optional>
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

	//Per cell, on the CPU only: downstream direction of the nearest course (unit vector along map x, y) and its slope (height per texel)
	struct FlowTexel {
		float x = 0.0f, y = 0.0f;
		float slope = 0.0f;
	};

	struct Network {
		//Cell (x, y) covers map position origin + (x, y) * cellSize (its center)
		glm::vec2 origin = glm::vec2(0.0f);
		float cellSize = 1.0f;
		Grid<RiverTexel> rivers;
		Grid<LevelTexel> levels;
		Grid<FlowTexel> flow;
		//The terrain the network was computed from (heights at the cell centers, without rivers)
		HeightField ground;
		//Statistics for the UI
		int riverCells = 0, lakeCells = 0, lakes = 0;
	};

	//@param terrain - heights at the cell centers, without rivers
	//@param origin - map position of cell (0, 0)
	//@param cellSize - map texels between cells
	//@param waterHeight - sea level (TerrainConfig::WaterHeight)
	Network Compute(const HeightField& terrain, glm::vec2 origin, float cellSize, float waterHeight, const rivers::RiverConfig& config);

	//Vertex of the water surface of rivers and lakes: map position (texels), surface height, flow velocity (metres per second along map x, y)
	struct WaterVertex {
		float x = 0.0f, y = 0.0f;
		float level = 0.0f;
		float flowX = 0.0f, flowY = 0.0f;
	};

	//Surface of the water of rivers (in their channel) and lakes at a map position, sampled as common/rivers.glsl samples the network;
	//-infinity where there is none (also outside the region)
	float WaterLevelAt(const Network& network, glm::vec2 position);

	//Speed of the water (m/s) on a slope given as height per length (world units over world units)
	float FlowSpeed(float slope);

	//Triangles (3 vertices each) of the water surface of rivers and lakes above the sea, one quad between 4 neighbouring cell centres where
	//any of them is wet: in a lake, or within the river half width + 1.5 cells of a course. Only lakes and channels (where the bed is carved
	//under the water) keep their level everywhere; elsewhere (dry corners, the river banks) the surface sinks under the ground, so it never hangs
	//above lower terrain (a lake outlet, a slope beside a river) and the terrain drawn before it (depth test) cuts the shore line per pixel
	//@param waterHeight - sea level; water not above it is left to the sea surface
	//@param texelsPerHeight - map texels as long as the height 1 is high (TerrainGen: 1024 world units / 4 per texel), for the flow speed
	std::vector<WaterVertex> BuildWaterMesh(const Network& network, float waterHeight, float texelsPerHeight);

	//The region following the camera in the infinite world jumps over a fixed grid, so a place gets the same network whichever region the camera
	//came from, and the cells of every region lie on the same map positions (the same terrain samples)
	//@return grid step in map texels: a quarter of regionSize down to whole cells, at least one cell
	float RegionStep(int regionSize, int cellSize);
	//@param position - map position (texels) to centre the region on, e.g. under the camera
	//@return the nearest grid point
	glm::vec2 SnapRegionCenter(glm::vec2 position, int regionSize, int cellSize);
	//Where a network was computed: its middle (also off the grid, e.g. set by the partial map) and the grid step of its size, kept with it
	//so that editing the region size does not move the region before the edit is applied
	struct RegionPlace {
		glm::vec2 center = glm::vec2(0.0f);
		float step = 1.0f;
	};
	//@return true if position is farther than the step of the region from its middle along x or y: time to compute the region around it
	bool LeftRegion(const RegionPlace& region, glm::vec2 position);
	//Region of a new network: around mapCenter on the partial map; in the infinite world around the grid point under the camera when following it,
	//else where the current region is (mapCenter if none yet)
	RegionPlace PlaceRegion(bool infiniteWorld, bool followCamera, const std::optional<RegionPlace>& current, glm::vec2 cameraPosition, glm::vec2 mapCenter,
		int regionSize, int cellSize);
}
