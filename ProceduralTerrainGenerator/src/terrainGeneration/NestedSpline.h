#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "WorldConfig.h"

//The NESTED terrain method (TerrainConfig::nested): height = a curve over continentalness C whose points are numbers or curves over
//mountainousness M, whose points are numbers or curves over weirdness W. A curve is evaluated like SampleSpline evaluates SplinePoints
//(the same segment, straight or monotone PCHIP), with the value of each point taken at this place: the constant, or its curve at the
//next noise. The rule is here (tests, the editor) and in common/terrain.glsl (generation); --gpu-check compares the two (section nested:).
namespace nested {
	constexpr int minPoints = 3;		//PCHIP end slopes need three points
	constexpr int maxPoints = 16;
	constexpr int maxDepth = 3;			//C, M, W
	constexpr int maxWCurves = 64;
	constexpr int maxCurves = 1 + maxPoints + maxWCurves;

	//Height at the noise values c, m, w (clamped to [-1, 1]): root at c, its children at m, theirs at w
	double Evaluate(const NestedCurve& root, double c, double m, double w);
	//Value of curve at depth 0 / 1 / 2, which reads c / m / w; the children read the next noise
	double EvaluateAt(const NestedCurve& curve, int depth, double c, double m, double w);

	//A tree giving the LAYERED terrain of layered at the knots of its three splines (between continentalness knots close to it),
	//with the sea level of LAYERED. A spline of more than maxPoints points keeps the ends and the points with the largest change
	NestedTerrain FromLayered(const TerrainConfig& layered);
	//The tree generation uses: t.nested.root, or (still empty) the tree from the LAYERED curves of t, made in scratch
	const NestedCurve& RootFor(const TerrainConfig& t, NestedCurve& scratch);

	//The tree for the GPU (WorldInput in common/world_input.glsl, std430): curves[0] = root, the knots of a curve in a row
	struct GpuCurve {
		int32_t firstKnot, count, cubic, pad;
	};
	struct GpuKnot {
		float x, value;
		int32_t child;	//index of the curve of this point, -1 = the constant value
		int32_t pad;
	};
	struct Flat {
		std::vector<GpuCurve> curves;
		std::vector<GpuKnot> knots;
	};
	Flat Flatten(const NestedCurve& root);

	//Empty for a valid tree, otherwise the first broken rule: minPoints..maxPoints points, x strictly ascending from -1 to 1,
	//constant values finite in [0, 1], at most one sub-curve per point, no sub-curves in W-curves, at most maxWCurves W-curves
	std::string Problem(const NestedCurve& root);

	//path = indices of points from the root to an open curve (at most 2: root -> M-curve -> W-curve). Trims it to the longest
	//start that still names points holding curves
	//@return true if path was valid as it was
	bool ValidPath(const NestedCurve& root, std::vector<int>& path);

	//Same points, values, sub-curves and cubic flags
	bool Equal(const NestedCurve& a, const NestedCurve& b);
}
