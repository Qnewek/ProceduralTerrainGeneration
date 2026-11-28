#include "SplineSampling.h"

#include <algorithm>
#include <cmath>

namespace {
	//Slopes of a monotone cubic Hermite spline (PCHIP, Fritsch-Carlson): 0 where the data turns (a peak or a valley point)
	//or is flat, a weighted harmonic mean of the neighbouring secants elsewhere. With these slopes every piece stays
	//between the values of its two points, so the curve never leaves the range of the points
	std::vector<double> MonotoneSlopes(const std::vector<double>& x, const std::vector<double>& y)
	{
		const size_t n = x.size();
		if (n < 2 || y.size() != n) {
			return std::vector<double>(n, 0.0);
		}
		std::vector<double> h(n - 1), secant(n - 1), slope(n, 0.0);
		for (size_t i = 0; i + 1 < n; i++) {
			h[i] = x[i + 1] - x[i];
			secant[i] = h[i] > 0.0 ? (y[i + 1] - y[i]) / h[i] : 0.0;
		}
		if (n == 2) {
			slope[0] = slope[1] = secant[0];
			return slope;
		}
		for (size_t i = 1; i + 1 < n; i++) {
			if (secant[i - 1] * secant[i] > 0.0) {
				const double w1 = 2.0 * h[i] + h[i - 1], w2 = h[i] + 2.0 * h[i - 1];
				slope[i] = (w1 + w2) / (w1 / secant[i - 1] + w2 / secant[i]);
			}
		}
		//Ends: a one-sided three-point estimate, limited so that it keeps the end piece monotone
		const auto endSlope = [](double h0, double h1, double d0, double d1) {
			if (!(h0 + h1 > 0.0)) {
				return 0.0;
			}
			double s = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
			if (s * d0 <= 0.0) {
				return 0.0;
			}
			if (d0 * d1 <= 0.0 && std::abs(s) > 3.0 * std::abs(d0)) {
				s = 3.0 * d0;
			}
			return s;
		};
		slope[0] = endSlope(h[0], h[1], secant[0], secant[1]);
		slope[n - 1] = endSlope(h[n - 2], h[n - 3], secant[n - 2], secant[n - 3]);
		return slope;
	}

	//Value at at; slope is empty for straight segments. Outside the points (only if they do not reach -1 or 1) the end value is kept
	double ValueAt(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& slope, double at)
	{
		if (x.empty() || x.size() != y.size()) {
			return 0.0;
		}
		if (x.size() == 1) {
			return y[0];
		}
		at = std::clamp(at, x.front(), x.back());
		size_t piece = 0;
		while (piece + 2 < x.size() && at > x[piece + 1]) {
			piece++;
		}
		const double h = x[piece + 1] - x[piece];
		if (!(h > 0.0)) {
			return y[piece];
		}
		const double t = (at - x[piece]) / h;
		if (!slope.empty()) {
			//Cubic Hermite basis
			const double t2 = t * t, t3 = t2 * t;
			return (2.0 * t3 - 3.0 * t2 + 1.0) * y[piece] + (t3 - 2.0 * t2 + t) * h * slope[piece]
				+ (-2.0 * t3 + 3.0 * t2) * y[piece + 1] + (t3 - t2) * h * slope[piece + 1];
		}
		return y[piece] + t * (y[piece + 1] - y[piece]);
	}
}

double EvaluateSpline(const SplinePoints& points, double at)
{
	return ValueAt(points.x, points.y, points.cubic ? MonotoneSlopes(points.x, points.y) : std::vector<double>(), at);
}

std::vector<float> SampleSpline(const SplinePoints& points, int count)
{
	if (count <= 0) {
		return {};
	}
	const std::vector<double> slope = points.cubic ? MonotoneSlopes(points.x, points.y) : std::vector<double>();
	if (count == 1) {
		return { static_cast<float>(ValueAt(points.x, points.y, slope, 0.0)) };
	}
	std::vector<float> samples(count);
	for (int i = 0; i < count; i++) {
		samples[i] = static_cast<float>(ValueAt(points.x, points.y, slope, -1.0 + 2.0 * i / (count - 1)));
	}
	return samples;
}
