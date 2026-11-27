#include "doctest/doctest.h"

#include <algorithm>
#include <cmath>

#include "NestedSpline.h"
#include "SplineSampling.h"

namespace {
	NestedCurve Constants(std::vector<double> x, std::vector<double> y, bool cubic = false)
	{
		NestedCurve curve;
		curve.cubic = cubic;
		for (size_t i = 0; i < x.size(); i++) {
			curve.points.push_back({ x[i], y[i], {} });
		}
		return curve;
	}

	//Root {(-1, 0), (0, M-curve), (1, 1)}, M-curve {(-1, 0.2), (0, W-curve), (1, 0.8)}, W-curve {(-1, 0.1), (0, 0.5), (1, 0.9)}
	NestedCurve ThreeLevels(bool cubic)
	{
		NestedCurve w = Constants({ -1.0, 0.0, 1.0 }, { 0.1, 0.5, 0.9 }, cubic);
		NestedCurve m = Constants({ -1.0, 0.0, 1.0 }, { 0.2, 0.0, 0.8 }, cubic);
		m.points[1].sub.push_back(w);
		NestedCurve root = Constants({ -1.0, 0.0, 1.0 }, { 0.0, 0.0, 1.0 }, cubic);
		root.points[1].sub.push_back(m);
		return root;
	}

	double Smoothstep(double edge0, double edge1, double x)
	{
		const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
		return t * t * (3.0 - 2.0 * t);
	}

	//A root of rootPoints points, each with an M-curve of mPoints points, each with a W-curve
	NestedCurve WithWCurves(int rootPoints, int mPoints)
	{
		const auto evenly = [](int n) {
			std::vector<double> x(n);
			for (int i = 0; i < n; i++) {
				x[i] = -1.0 + 2.0 * i / (n - 1);
			}
			return x;
		};
		NestedCurve m = Constants(evenly(mPoints), std::vector<double>(mPoints, 0.5));
		for (NestedPoint& p : m.points) {
			p.sub.push_back(Constants({ -1.0, 0.0, 1.0 }, { 0.1, 0.5, 0.9 }));
		}
		NestedCurve root = Constants(evenly(rootPoints), std::vector<double>(rootPoints, 0.5));
		for (NestedPoint& p : root.points) {
			p.sub.push_back(m);
		}
		return root;
	}
}

TEST_CASE("EvaluateSpline equals SampleSpline at its samples, linear and cubic")
{
	for (bool cubic : { false, true }) {
		CAPTURE(cubic);
		SplinePoints s = TerrainConfig::Default().continentalnessSpline;
		s.cubic = cubic;
		const std::vector<float> samples = SampleSpline(s, 257);
		for (int i = 0; i < 257; i++) {
			CAPTURE(i);
			CHECK(EvaluateSpline(s, -1.0 + 2.0 * i / 256) == doctest::Approx(samples[i]).epsilon(1e-6));
		}
	}
}

TEST_CASE("A nested curve of constants equals the same SplinePoints")
{
	const SplinePoints peaks = TerrainConfig{}.peaksSpline;
	NestedCurve root = Constants(peaks.x, peaks.y, true);
	SplinePoints cubicPeaks = peaks;
	cubicPeaks.cubic = true;
	for (double c : { -1.0, -0.5, -0.37, 0.999, 1.0 }) {
		CAPTURE(c);
		CHECK(nested::Evaluate(root, c, 0.3, -0.7) == doctest::Approx(EvaluateSpline(cubicPeaks, c)).epsilon(1e-9));
	}
	//Not a constant 0: the curve really rises
	CHECK(nested::Evaluate(root, 1.0, 0.0, 0.0) == doctest::Approx(1.0));
}

TEST_CASE("Children of nested curves are read at the next noise")
{
	const NestedCurve root = ThreeLevels(false);
	CHECK(nested::Evaluate(root, 0.0, 0.0, -0.5) == doctest::Approx(0.3));
	CHECK(nested::Evaluate(root, 0.0, 0.0, 1.0) == doctest::Approx(0.9));
	CHECK(nested::Evaluate(root, 0.0, 1.0, -0.5) == doctest::Approx(0.8));
	CHECK(nested::Evaluate(root, 0.5, 0.0, 0.0) == doctest::Approx(0.75));
	//Inputs outside [-1, 1] take the ends
	CHECK(nested::Evaluate(root, 3.0, 0.0, 0.0) == doctest::Approx(1.0));
	CHECK(nested::Evaluate(root, 0.0, 0.0, -7.0) == doctest::Approx(0.1));
	//EvaluateAt reads the noise of its depth
	CHECK(nested::EvaluateAt(root.points[1].sub[0], 1, 0.9, 0.0, -0.5) == doctest::Approx(0.3));
	CHECK(nested::EvaluateAt(root.points[1].sub[0].points[1].sub[0], 2, 0.9, 0.9, -0.5) == doctest::Approx(0.3));
}

TEST_CASE("Cubic nested curves stay within their knot values")
{
	const NestedCurve root = ThreeLevels(true);
	size_t outside = 0;
	for (int i = 0; i <= 40; i++) {
		for (int j = 0; j <= 40; j++) {
			for (int k = 0; k <= 40; k++) {
				const double h = nested::Evaluate(root, -1.0 + i / 20.0, -1.0 + j / 20.0, -1.0 + k / 20.0);
				outside += std::isfinite(h) && h >= 0.0 && h <= 1.0 ? 0 : 1;
			}
		}
	}
	CHECK(outside == 0);
	//Not straight segments: at w = 1 the M-curve has knots 0.2, 0.9, 0.8 (straight halfway = 0.55, PCHIP = 0.6875)
	CHECK(nested::Evaluate(root, 0.0, -0.5, 1.0) == doctest::Approx(0.6875));
}

TEST_CASE("FromLayered equals LAYERED at the knots of its splines")
{
	for (bool cubic : { false, true }) {
		CAPTURE(cubic);
		TerrainConfig t = TerrainConfig::Default();
		t.continentalnessSpline.cubic = t.mountainousnessSpline.cubic = t.peaksSpline.cubic = cubic;
		const NestedTerrain tree = nested::FromLayered(t);
		CHECK(nested::Problem(tree.root).empty());
		CHECK(tree.seaLevel == doctest::Approx(t.WaterHeight()));
		const double ms = t.mountainShare;
		for (double c : t.continentalnessSpline.x) {
			for (double m : t.mountainousnessSpline.x) {
				for (double w : t.peaksSpline.x) {
					CAPTURE(c);
					CAPTURE(m);
					CAPTURE(w);
					const double base = EvaluateSpline(t.continentalnessSpline, c);
					const double land = Smoothstep(t.seaLevel, t.seaLevel + t.coastWidth, base);
					const double expected = base * (1.0 - ms)
						+ land * EvaluateSpline(t.mountainousnessSpline, m) * EvaluateSpline(t.peaksSpline, w) * ms;
					CHECK(nested::Evaluate(tree.root, c, m, w) == doctest::Approx(expected).epsilon(1e-6));
				}
			}
		}
	}
}

TEST_CASE("FromLayered keeps at most 16 points of a long spline, with its ends")
{
	TerrainConfig t = TerrainConfig::Default();
	SplinePoints& s = t.continentalnessSpline;
	s.x.clear();
	s.y.clear();
	for (int i = 0; i < 30; i++) {
		s.x.push_back(-1.0 + 2.0 * i / 29);
		s.y.push_back(i == 10 ? 1.0 : 0.0);
	}
	const NestedTerrain tree = nested::FromLayered(t);
	REQUIRE(tree.root.points.size() == 16);
	CHECK(tree.root.points.front().x == -1.0);
	CHECK(tree.root.points.back().x == 1.0);
	CHECK(nested::Problem(tree.root).empty());
	//The peak is kept
	CHECK(std::any_of(tree.root.points.begin(), tree.root.points.end(), [&](const NestedPoint& p) { return p.x == s.x[10]; }));
}

TEST_CASE("FromLayered of dense splines stays within the limit of W-curves")
{
	//16 continentalness points all on land and 16 mountainousness points all above 0 would give 256 W-curves
	TerrainConfig t = TerrainConfig::Default();
	SplinePoints dense;
	for (int i = 0; i < 16; i++) {
		dense.x.push_back(-1.0 + 2.0 * i / 15);
		dense.y.push_back(0.5 + 0.03 * i);
	}
	t.continentalnessSpline = dense;
	t.mountainousnessSpline = dense;
	const NestedTerrain tree = nested::FromLayered(t);
	CHECK(nested::Problem(tree.root).empty());
	CHECK(tree.root.points.size() == 16);
	//Still exact at the knots kept: the first root point, its first M point, every peaks knot
	const double ms = t.mountainShare;
	const double land = Smoothstep(t.seaLevel, t.seaLevel + t.coastWidth, dense.y[0]);
	for (double w : t.peaksSpline.x) {
		CAPTURE(w);
		CHECK(nested::Evaluate(tree.root, -1.0, -1.0, w)
			== doctest::Approx(dense.y[0] * (1.0 - ms) + land * dense.y[0] * EvaluateSpline(t.peaksSpline, w) * ms).epsilon(1e-6));
	}
}

TEST_CASE("Equal compares nested trees")
{
	CHECK(nested::Equal(ThreeLevels(false), ThreeLevels(false)));
	CHECK_FALSE(nested::Equal(ThreeLevels(false), ThreeLevels(true)));
	NestedCurve other = ThreeLevels(false);
	other.points[1].sub[0].points[1].sub[0].points[2].y = 0.95;
	CHECK_FALSE(nested::Equal(ThreeLevels(false), other));
	other = ThreeLevels(false);
	other.points[1].sub[0].points[1].sub.clear();
	CHECK_FALSE(nested::Equal(ThreeLevels(false), other));
	other = ThreeLevels(false);
	other.points[0].x = -0.999;
	CHECK_FALSE(nested::Equal(ThreeLevels(false), other));
}

TEST_CASE("RootFor makes the tree from LAYERED while the tree is empty")
{
	TerrainConfig t = TerrainConfig::Default();
	t.method = EvaluationMethod::NESTED;
	NestedCurve scratch;
	const NestedCurve& made = nested::RootFor(t, scratch);
	CHECK(&made == &scratch);
	CHECK(made.points.size() == t.continentalnessSpline.x.size());
	t.nested.root = ThreeLevels(false);
	CHECK(&nested::RootFor(t, scratch) == &t.nested.root);
}

TEST_CASE("Flatten keeps the nested tree: root first, children after their parents")
{
	const nested::Flat flat = nested::Flatten(ThreeLevels(true));
	REQUIRE(flat.curves.size() == 3);
	CHECK(flat.knots.size() == 9);
	CHECK(flat.curves[0].count == 3);
	CHECK(flat.curves[0].firstKnot == 0);
	for (size_t c = 0; c < flat.curves.size(); c++) {
		const nested::GpuCurve& curve = flat.curves[c];
		CHECK(curve.cubic == 1);
		for (int k = curve.firstKnot; k < curve.firstKnot + curve.count; k++) {
			const int child = flat.knots[k].child;
			CHECK((child == -1 || (child > static_cast<int>(c) && child < static_cast<int>(flat.curves.size()))));
		}
	}
	//Root: (-1, 0), (0, child), (1, 1)
	CHECK(flat.knots[0].x == -1.0f);
	CHECK(flat.knots[0].value == 0.0f);
	CHECK(flat.knots[1].child >= 1);
	CHECK(flat.knots[2].value == 1.0f);
	//The W-curve holds the constants 0.1, 0.5, 0.9
	const nested::GpuCurve& m = flat.curves[flat.knots[1].child];
	const nested::GpuCurve& w = flat.curves[flat.knots[m.firstKnot + 1].child];
	CHECK(flat.knots[w.firstKnot + 1].value == doctest::Approx(0.5f));
	CHECK(flat.knots[w.firstKnot + 2].child == -1);
}

TEST_CASE("Problem names broken nested trees")
{
	CHECK(nested::Problem(ThreeLevels(false)).empty());
	CHECK(nested::Problem(WithWCurves(4, 16)).empty());

	CHECK(!nested::Problem(Constants({ -1.0, 1.0 }, { 0.0, 1.0 })).empty());
	std::vector<double> x17, y17(17, 0.5);
	for (int i = 0; i < 17; i++) {
		x17.push_back(-1.0 + i / 8.0);
	}
	CHECK(!nested::Problem(Constants(x17, y17)).empty());
	CHECK(!nested::Problem(Constants({ -1.0, 0.5, 0.2, 1.0 }, { 0.0, 0.1, 0.2, 0.3 })).empty());
	CHECK(!nested::Problem(Constants({ -1.0, 0.0, 0.0, 1.0 }, { 0.0, 0.1, 0.2, 0.3 })).empty());
	CHECK(!nested::Problem(Constants({ -0.9, 0.0, 1.0 }, { 0.0, 0.1, 0.2 })).empty());
	CHECK(!nested::Problem(Constants({ -1.0, 0.0, 0.9 }, { 0.0, 0.1, 0.2 })).empty());
	CHECK(!nested::Problem(Constants({ -1.0, 0.0, 1.0 }, { 0.0, 1.5, 0.2 })).empty());
	CHECK(!nested::Problem(Constants({ -1.0, 0.0, 1.0 }, { 0.0, std::nan(""), 0.2 })).empty());
	//A W-curve with a curve of its own
	NestedCurve deep = ThreeLevels(false);
	deep.points[1].sub[0].points[1].sub[0].points[0].sub.push_back(Constants({ -1.0, 0.0, 1.0 }, { 0.0, 0.0, 0.0 }));
	CHECK(!nested::Problem(deep).empty());
	//Two curves on one point
	NestedCurve twice = ThreeLevels(false);
	twice.points[0].sub.push_back(Constants({ -1.0, 0.0, 1.0 }, { 0.0, 0.0, 0.0 }));
	twice.points[0].sub.push_back(Constants({ -1.0, 0.0, 1.0 }, { 0.0, 0.0, 0.0 }));
	CHECK(!nested::Problem(twice).empty());
	//A broken child
	NestedCurve badChild = ThreeLevels(false);
	badChild.points[1].sub[0].points[0].y = -0.1;
	CHECK(!nested::Problem(badChild).empty());
	//65 W-curves
	CHECK(!nested::Problem(WithWCurves(5, 13)).empty());
}

TEST_CASE("ValidPath trims a path whose points lost their sub-curves")
{
	NestedCurve root = ThreeLevels(false);
	std::vector<int> path{ 1, 1 };
	CHECK(nested::ValidPath(root, path));
	CHECK(path == std::vector<int>{ 1, 1 });

	//The W-curve became a constant: the path ends at the M-curve
	root.points[1].sub[0].points[1].sub.clear();
	CHECK_FALSE(nested::ValidPath(root, path));
	CHECK(path == std::vector<int>{ 1 });

	//The point holding the M-curve was removed
	root.points.erase(root.points.begin() + 1);
	CHECK_FALSE(nested::ValidPath(root, path));
	CHECK(path.empty());

	//An index past the points, and a path deeper than the W level
	root = ThreeLevels(false);
	path = { 7 };
	CHECK_FALSE(nested::ValidPath(root, path));
	CHECK(path.empty());
	path = { 1, 1, 0 };
	CHECK_FALSE(nested::ValidPath(root, path));
	CHECK(path == std::vector<int>{ 1, 1 });
	path = { -1 };
	CHECK_FALSE(nested::ValidPath(root, path));
	CHECK(path.empty());
}

TEST_CASE("WaterHeight and HasSea follow the terrain method")
{
	TerrainConfig t = TerrainConfig::Default();
	t.nested.seaLevel = 0.27f;
	CHECK(t.HasSea());
	CHECK(t.WaterHeight() == doctest::Approx(t.seaLevel * (1.0f - t.mountainShare)));
	t.method = EvaluationMethod::NESTED;
	CHECK(t.HasSea());
	CHECK(t.WaterHeight() == doctest::Approx(0.27f));
	t.method = EvaluationMethod::SPLINE_COMBINE;
	CHECK(!t.HasSea());
	CHECK(t.WaterHeight() == 0.0f);
	t.method = EvaluationMethod::LINEAR_COMBINE;
	CHECK(!t.HasSea());
}
