#pragma once

#include "glm/glm.hpp"

#include "Grid.h"

//Shape of a height map: gradient and Laplacian of every texel. The application computes them on the GPU
//(GpuSurfacePass, surface_compute.shader); this CPU version is the reference for tests and --gpu-check.
namespace surface
{
	//Derivatives of the raw height (before the vertical scale of the view) per world unit,
	//so a different heightScale only multiplies them and needs no new map
	struct Derivatives {
		float dx = 0.0f;		//change of height along x (map columns)
		float dy = 0.0f;		//change of height along y (map rows, world Z)
		float laplacian = 0.0f;	//d2h/dx2 + d2h/dy2; positive in hollows and valleys, negative on hills and ridges
		float reserved = 0.0f;	//unused, keeps the layout of an RGBA32F texel
	};
	static_assert(sizeof(Derivatives) == 4 * sizeof(float));

	using DerivativeMap = Grid<Derivatives>;

	//Central differences inside the map; at the edges the gradient is one-sided and the Laplacian is taken
	//from the nearest texel that has both neighbours (0 along an axis shorter than 3 texels)
	//@param texelSize - distance between neighbouring texels in world units
	DerivativeMap Compute(const HeightField& height, float texelSize);

	//Unit normal of the surface displaced by heightScale; x and z follow map x and y, y points up
	glm::vec3 Normal(const Derivatives& d, float heightScale);
	//Angle between the surface displaced by heightScale and the horizontal plane, in radians
	float Slope(const Derivatives& d, float heightScale);
	//Laplacian of the surface displaced by heightScale, in 1 / world unit
	float Curvature(const Derivatives& d, float heightScale);
}
