#include "doctest/doctest.h"

#include <algorithm>

#include "SplineSampling.h"

TEST_CASE("A sampled spline passes through its points and spans [-1, 1]")
{
	//Points on samples: with 5 samples the noise values are -1, -0.5, 0, 0.5, 1
	for (bool cubic : { false, true }) {
		CAPTURE(cubic);
		const SplinePoints points{ { -1.0, 0.0, 1.0 }, { 0.2, 0.9, 0.4 }, cubic };
		const std::vector<float> samples = SampleSpline(points, 5);
		REQUIRE(samples.size() == 5);
		CHECK(samples[0] == doctest::Approx(0.2f));
		CHECK(samples[2] == doctest::Approx(0.9f));
		CHECK(samples[4] == doctest::Approx(0.4f));
	}
}

TEST_CASE("A linear spline is straight between its points, a cubic one bends")
{
	const SplinePoints linear{ { -1.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 }, false };
	const std::vector<float> samples = SampleSpline(linear, 5);
	CHECK(samples[1] == doctest::Approx(0.5f));
	CHECK(samples[3] == doctest::Approx(0.5f));

	SplinePoints cubic = linear;
	cubic.cubic = true;
	//The smooth curve through a peak is above the straight segments between the points, and flat on the peak
	const std::vector<float> smooth = SampleSpline(cubic, 1001);
	CHECK(smooth[250] > 0.5f);
	CHECK(smooth[499] == doctest::Approx(smooth[500]).epsilon(1e-4));
}

TEST_CASE("A cubic spline never leaves the range of the two points around it")
{
	//A steep rise next to flat parts and a narrow peak: a natural cubic spline overshoots on both
	const SplinePoints cases[] = {
		{ { -1.0, -0.9, 1.0 }, { 0.0, 1.0, 1.0 }, true },
		{ { -1.0, -0.1, 0.0, 0.1, 1.0 }, { 0.0, 0.0, 1.0, 0.0, 0.0 }, true },
		{ { -1.0, -0.78, -0.37, -0.2, 0.05, 0.45, 0.55, 1.0 }, { 0.0, 0.1, 0.2, 0.3, 0.3, 0.6, 0.6, 1.0 }, true },
		{ { -1.0, 0.0, 0.01, 1.0 }, { 1.0, 1.0, 0.0, 0.0 }, true },
	};
	constexpr int count = 2001;
	for (const SplinePoints& points : cases) {
		CAPTURE(points.x.size());
		const std::vector<float> samples = SampleSpline(points, count);
		int outside = 0;
		for (int i = 0; i < count; i++) {
			const double x = -1.0 + 2.0 * i / (count - 1);
			//Piece holding x
			size_t piece = 0;
			while (piece + 2 < points.x.size() && x > points.x[piece + 1]) {
				piece++;
			}
			const double low = std::min(points.y[piece], points.y[piece + 1]), high = std::max(points.y[piece], points.y[piece + 1]);
			outside += samples[i] < low - 1e-6 || samples[i] > high + 1e-6 ? 1 : 0;
		}
		CHECK(outside == 0);
	}
}
