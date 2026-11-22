#pragma once

#include <cstdint>
#include <optional>

#include "glm/glm.hpp"

#include "Grid.h"

//Brushes modelling the working copy of the "Imported map" mode. This is the CPU reference of sculpt_compute.shader (the GPU applies the
//brushes while painting; --gpu-check compares both), plus the cursor picking shared by the 3D view and the 2D map
namespace sculpt {
	enum class Brush { RAISE, LOWER, SMOOTH, FLATTEN, NOISE };
	const char* BrushName(Brush brush);

	struct BrushConfig {
		Brush brush = Brush::RAISE;
		float radius = 32.0f;		//map pixels
		float strength = 10.0f;		//RAISE, LOWER, NOISE: metres per second; SMOOTH, FLATTEN: part of the way to the target per second
		float hardness = 0.5f;		//full strength up to hardness * radius, then a smooth fall to 0 at the radius
		float noiseScale = 16.0f;	//NOISE: map pixels per noise cell
	};

	//One use of a brush (one frame of a stroke)
	struct Dab {
		glm::vec2 center = glm::vec2(0.0f);	//map texels
		float amount = 0.0f;	//RAISE, LOWER, NOISE: map units; SMOOTH, FLATTEN: part of the way, 0 to 1
		float target = 0.0f;	//FLATTEN: height in map units
		uint32_t seed = 0;		//NOISE
		float limit = 1.5f;		//heights are kept within [-limit, limit] (the terrain culling bound, TerrainRenderSettings::heightBound)
	};

	//World units per map texel; equal to TerrainView::worldUnitsPerTexel (checked where both are seen)
	inline constexpr float worldUnitsPerTexel = 4.0f;

	//@return 1 up to hardness * radius, smoothstep down to 0 at radius, 0 beyond; hardness >= 1 = a sharp edge
	float Falloff(float distance, float radius, float hardness);
	//@return x, y, width, height of the texels within radius of the dab, inside the map; width 0 when none
	glm::ivec4 Area(const Dab& dab, float radius, glm::ivec2 mapSize);
	//SMOOTH averages 3x3 texels this far apart, so a large brush smooths large shapes
	int SmoothSpacing(float radius);
	//Value noise from an integer hash, two octaves, in [-1, 1]; the same formula as sculpt_compute.shader
	float NoiseAt(glm::vec2 texel, float scale, uint32_t seed);
	//Applies a dab; every texel reads the heights from before the dab (the GPU reads a copy)
	void Apply(HeightField& units, const BrushConfig& config, const Dab& dab);

	//Seconds of painting at full strength a stroke has room for before the limit stops it
	inline constexpr float strokeHeadroomSeconds = 30.0f;
	//Dab::limit (map units) for a stroke of RAISE, LOWER or NOISE: room for strokeHeadroomSeconds above the largest |height|, so a map whose unit
	//is small (a flat map has 1 m) is not stopped at 1.5 units; never below 1.5. The map keeps its units (and colours) during the stroke,
	//the terrain culling gets the same bound (TerrainRenderSettings::heightBound)
	//@param largestMetres - largest |height| of the working copy in metres
	float StrokeLimit(float metresPerUnit, float largestMetres, float metresPerSecond);

	//Map texel under a window pixel, from the depth of the drawn scene there
	//@param windowPixel - framebuffer pixels, origin at the bottom left (as glReadPixels)
	//@param depth - depth buffer value; 1 = nothing drawn there
	//@param viewport - x, y, width, height of the viewport
	//@param viewProjection - the matrices the terrain was drawn with (also the top-down camera of the 2D map)
	//@return none over the sky or outside the map (a map without an offset, texel (0, 0) at its corner, x along world X, y along world Z)
	std::optional<glm::vec2> PickTexel(glm::vec2 windowPixel, float depth, glm::ivec4 viewport, const glm::mat4& viewProjection, float modelScale,
		glm::ivec2 mapSize);
	//World position of a map texel at a height in map units
	glm::vec3 TexelToWorld(glm::vec2 texel, float heightUnits, float heightScale, float modelScale, glm::ivec2 mapSize);
}
