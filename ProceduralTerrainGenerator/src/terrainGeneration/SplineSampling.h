#pragma once

#include <vector>

#include "WorldConfig.h"

//Values of a spline at count (>= 2) evenly spaced noise values from -1 to 1 (both included): straight segments, or with points.cubic
//a smooth monotone cubic (PCHIP) that never leaves the range of the points, so points in [0, 1] give a curve in [0, 1].
//The GPU gets splines as such a table and the spline editor draws it, so both show the same curve.
//points must hold at least 3 points with strictly ascending x
std::vector<float> SampleSpline(const SplinePoints& points, int count);
