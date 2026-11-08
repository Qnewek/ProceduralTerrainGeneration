#include "doctest/doctest.h"

#include <cmath>
#include <functional>
#include <numbers>

#include "SurfaceDerivatives.h"

namespace {
	constexpr float texelSize = 4.0f;

	//Not square, so swapped axes are detected
	HeightField MakeMap(int width, int height, const std::function<float(float, float)>& f)
	{
		HeightField map(width, height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				map.At(x, y) = f(static_cast<float>(x), static_cast<float>(y));
			}
		}
		return map;
	}
}

TEST_CASE("A flat map has no slope and no curvature")
{
	const surface::DerivativeMap d = surface::Compute(MakeMap(7, 5, [](float, float) { return 0.3f; }), texelSize);
	for (size_t i = 0; i < d.Size(); i++) {
		CHECK(d[i].dx == 0.0f);
		CHECK(d[i].dy == 0.0f);
		CHECK(d[i].laplacian == 0.0f);
	}
	const glm::vec3 normal = surface::Normal(d.At(3, 2), 256.0f);
	CHECK(normal.x == 0.0f);
	CHECK(normal.y == 1.0f);
	CHECK(normal.z == 0.0f);
	CHECK(surface::Slope(d.At(3, 2), 256.0f) == 0.0f);
}

TEST_CASE("A plane has the same gradient everywhere, edges included")
{
	//Height rises by 0.5 per texel along x and falls by 0.25 along y
	const surface::DerivativeMap d = surface::Compute(MakeMap(7, 5, [](float x, float y) { return 0.5f * x - 0.25f * y; }), texelSize);
	for (int y = 0; y < d.GetHeight(); y++) {
		for (int x = 0; x < d.GetWidth(); x++) {
			CAPTURE(x);
			CAPTURE(y);
			CHECK(d.At(x, y).dx == doctest::Approx(0.5f / texelSize));
			CHECK(d.At(x, y).dy == doctest::Approx(-0.25f / texelSize));
			CHECK(d.At(x, y).laplacian == doctest::Approx(0.0f));
		}
	}
}

TEST_CASE("The normal leans away from the rising side and the slope follows the height scale")
{
	surface::Derivatives d;
	d.dx = 0.01f;
	//0.01 per world unit times 100 = a 45 degree slope down towards -x
	const glm::vec3 normal = surface::Normal(d, 100.0f);
	CHECK(normal.x == doctest::Approx(-std::sqrt(0.5f)));
	CHECK(normal.y == doctest::Approx(std::sqrt(0.5f)));
	CHECK(normal.z == doctest::Approx(0.0f));
	CHECK(surface::Slope(d, 100.0f) == doctest::Approx(std::numbers::pi_v<float> / 4.0f));
	CHECK(surface::Slope(d, 0.0f) == 0.0f);

	d = {};
	d.dy = -0.02f;
	CHECK(surface::Normal(d, 50.0f).z > 0.0f);
}

TEST_CASE("A bowl has a constant positive Laplacian and a hill a negative one")
{
	//h = x^2 + y^2 in texels: the second difference is 2 along each axis
	const surface::DerivativeMap bowl = surface::Compute(MakeMap(7, 5, [](float x, float y) { return x * x + y * y; }), texelSize);
	for (size_t i = 0; i < bowl.Size(); i++) {
		CHECK(bowl[i].laplacian == doctest::Approx(4.0f / (texelSize * texelSize)));
	}
	//Inside: central difference of x^2 is exactly 2x per texel; at the edge it is one-sided
	CHECK(bowl.At(3, 2).dx == doctest::Approx(6.0f / texelSize));
	CHECK(bowl.At(3, 2).dy == doctest::Approx(4.0f / texelSize));
	CHECK(bowl.At(0, 2).dx == doctest::Approx(1.0f / texelSize));
	CHECK(bowl.At(6, 2).dx == doctest::Approx(11.0f / texelSize));

	const surface::DerivativeMap hill = surface::Compute(MakeMap(7, 5, [](float x, float y) { return -(x - 3.0f) * (x - 3.0f) - (y - 2.0f) * (y - 2.0f); }), texelSize);
	CHECK(hill.At(3, 2).laplacian < 0.0f);
	CHECK(hill.At(3, 2).dx == doctest::Approx(0.0f));
	CHECK(surface::Curvature(hill.At(3, 2), 2.0f) == doctest::Approx(2.0f * hill.At(3, 2).laplacian));
}

TEST_CASE("A map narrower than 3 texels has a gradient but no second difference along that axis")
{
	const surface::DerivativeMap d = surface::Compute(MakeMap(2, 4, [](float x, float y) { return x + y * y; }), texelSize);
	CHECK(d.At(0, 1).dx == doctest::Approx(1.0f / texelSize));
	CHECK(d.At(1, 1).dx == doctest::Approx(1.0f / texelSize));
	//Only the y axis contributes: second difference of y^2 is 2
	CHECK(d.At(0, 1).laplacian == doctest::Approx(2.0f / (texelSize * texelSize)));

	const surface::DerivativeMap single = surface::Compute(MakeMap(1, 1, [](float, float) { return 1.0f; }), texelSize);
	CHECK(single.At(0, 0).dx == 0.0f);
	CHECK(single.At(0, 0).laplacian == 0.0f);
}
