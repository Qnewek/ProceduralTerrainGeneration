#include "NestedSpline.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "SplineSampling.h"

namespace nested {
	namespace {
		//Same as smoothstep in GLSL
		double Smoothstep(double edge0, double edge1, double x)
		{
			const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
			return t * t * (3.0 - 2.0 * t);
		}

		//At most limit (>= 3) points: the ends and the interior points with the largest change of y around them
		SplinePoints Trimmed(const SplinePoints& s, int limit)
		{
			const size_t n = s.x.size();
			if (n <= static_cast<size_t>(limit)) {
				return s;
			}
			std::vector<size_t> interior(n - 2);
			std::iota(interior.begin(), interior.end(), size_t(1));
			const auto change = [&](size_t i) { return std::abs(s.y[i] - s.y[i - 1]) + std::abs(s.y[i + 1] - s.y[i]); };
			std::stable_sort(interior.begin(), interior.end(), [&](size_t a, size_t b) { return change(a) > change(b); });
			interior.resize(limit - 2);
			interior.push_back(0);
			interior.push_back(n - 1);
			std::sort(interior.begin(), interior.end());
			SplinePoints out;
			out.cubic = s.cubic;
			for (size_t i : interior) {
				out.x.push_back(s.x[i]);
				out.y.push_back(s.y[i]);
			}
			return out;
		}

		std::string CurveProblem(const NestedCurve& curve, int depth, int& wCurves)
		{
			static const char* levels[] = { "continentalness", "mountainousness", "weirdness" };
			const std::string name = std::string("a curve over ") + levels[depth];
			if (depth == 2) {
				wCurves++;
			}
			const auto& p = curve.points;
			if (p.size() < static_cast<size_t>(minPoints) || p.size() > static_cast<size_t>(maxPoints)) {
				return name + " has " + std::to_string(p.size()) + " points, needs " + std::to_string(minPoints) + " to " + std::to_string(maxPoints);
			}
			if (p.front().x != -1.0 || p.back().x != 1.0) {
				return name + " has to start at -1 and end at 1";
			}
			for (size_t i = 0; i < p.size(); i++) {
				if (!std::isfinite(p[i].x) || (i > 0 && !(p[i].x > p[i - 1].x))) {
					return name + ": x of the points has to rise";
				}
				if (p[i].sub.size() > 1) {
					return name + ": a point holds more than one curve";
				}
				if (p[i].sub.empty()) {
					if (!(p[i].y >= 0.0 && p[i].y <= 1.0)) {
						return name + ": values have to be in [0, 1]";
					}
				}
				else {
					if (depth + 1 >= maxDepth) {
						return name + " cannot hold curves";
					}
					std::string problem = CurveProblem(p[i].sub[0], depth + 1, wCurves);
					if (!problem.empty()) {
						return problem;
					}
				}
			}
			return {};
		}

		int Add(const NestedCurve& curve, Flat& flat)
		{
			const int index = static_cast<int>(flat.curves.size());
			const int first = static_cast<int>(flat.knots.size());
			flat.curves.push_back({ first, static_cast<int32_t>(curve.points.size()), curve.cubic ? 1 : 0, 0 });
			flat.knots.resize(flat.knots.size() + curve.points.size());
			for (size_t i = 0; i < curve.points.size(); i++) {
				const NestedPoint& p = curve.points[i];
				const int child = p.sub.empty() ? -1 : Add(p.sub[0], flat);
				flat.knots[first + i] = { static_cast<float>(p.x), static_cast<float>(p.y), child, 0 };
			}
			return index;
		}
	}

	//EvaluateSpline of the knot values, but only the knots the segment needs are evaluated (2 for straight segments, up to 4 for PCHIP),
	//as terrain.glsl does: the editor evaluates the whole tree for every cell of its map, so this has to be fast
	double EvaluateAt(const NestedCurve& curve, int depth, double c, double m, double w)
	{
		const auto& p = curve.points;
		const size_t n = p.size();
		const auto knotValue = [&](size_t k) {
			return p[k].sub.empty() || depth + 1 >= maxDepth ? p[k].y : EvaluateAt(p[k].sub[0], depth + 1, c, m, w);
		};
		if (n == 0) {
			return 0.0;
		}
		if (n == 1) {
			return knotValue(0);
		}
		const double input = depth == 0 ? c : depth == 1 ? m : w;
		const double at = std::clamp(std::clamp(input, -1.0, 1.0), p.front().x, p.back().x);
		size_t segment = 0;
		while (segment + 2 < n && at > p[segment + 1].x) {
			segment++;
		}
		const bool cubic = curve.cubic && n >= static_cast<size_t>(minPoints);
		const size_t lo = cubic && segment > 0 ? segment - 1 : segment;
		const size_t hi = cubic ? std::min(segment + 2, n - 1) : segment + 1;
		double v[4] = {};
		for (size_t k = lo; k <= hi; k++) {
			v[k - lo] = knotValue(k);
		}
		const auto value = [&](size_t k) { return v[k - lo]; };
		const auto secant = [&](size_t i) { return (value(i + 1) - value(i)) / (p[i + 1].x - p[i].x); };

		const double h = p[segment + 1].x - p[segment].x;
		const double t = (at - p[segment].x) / h;
		const double y0 = value(segment), y1 = value(segment + 1);
		if (!cubic) {
			return y0 + t * (y1 - y0);
		}
		//Slopes of MonotoneSlopes (SplineSampling.cpp) at the two knots of the segment
		const auto endSlope = [](double h0, double h1, double d0, double d1) {
			double s = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
			if (s * d0 <= 0.0) {
				return 0.0;
			}
			if (d0 * d1 <= 0.0 && std::abs(s) > 3.0 * std::abs(d0)) {
				s = 3.0 * d0;
			}
			return s;
		};
		const auto slope = [&](size_t i) {
			if (i == 0) {
				return endSlope(p[1].x - p[0].x, p[2].x - p[1].x, secant(0), secant(1));
			}
			if (i == n - 1) {
				return endSlope(p[n - 1].x - p[n - 2].x, p[n - 2].x - p[n - 3].x, secant(n - 2), secant(n - 3));
			}
			const double before = secant(i - 1), after = secant(i);
			if (before * after > 0.0) {
				const double w1 = 2.0 * (p[i + 1].x - p[i].x) + (p[i].x - p[i - 1].x), w2 = (p[i + 1].x - p[i].x) + 2.0 * (p[i].x - p[i - 1].x);
				return (w1 + w2) / (w1 / before + w2 / after);
			}
			return 0.0;
		};
		const double t2 = t * t, t3 = t2 * t;
		return (2.0 * t3 - 3.0 * t2 + 1.0) * y0 + (t3 - 2.0 * t2 + t) * h * slope(segment)
			+ (-2.0 * t3 + 3.0 * t2) * y1 + (t3 - t2) * h * slope(segment + 1);
	}

	double Evaluate(const NestedCurve& root, double c, double m, double w)
	{
		return EvaluateAt(root, 0, c, m, w);
	}

	NestedTerrain FromLayered(const TerrainConfig& layered)
	{
		const SplinePoints cs = Trimmed(layered.continentalnessSpline, maxPoints);
		//Every land point of the C curve gets an M-curve with a W-curve per point of non-zero amplitude: with L land points the M-curves
		//keep at most maxWCurves / L points (L <= 16, so at least 4), which keeps the tree within maxWCurves
		int landPoints = 0;
		for (double base : cs.y) {
			landPoints += Smoothstep(layered.seaLevel, layered.seaLevel + layered.coastWidth, base) > 0.0 ? 1 : 0;
		}
		const int mLimit = landPoints > 0 ? std::clamp(maxWCurves / landPoints, minPoints, maxPoints) : maxPoints;
		const SplinePoints ms = Trimmed(layered.mountainousnessSpline, mLimit);
		const SplinePoints ps = Trimmed(layered.peaksSpline, maxPoints);
		const double share = layered.mountainShare;
		NestedTerrain tree;
		//WaterHeight() of LAYERED, whatever the method of layered is
		tree.seaLevel = layered.seaLevel * (1.0f - layered.mountainShare);
		tree.root.cubic = cs.cubic;
		for (size_t i = 0; i < cs.x.size(); i++) {
			const double base = cs.y[i];
			const double land = Smoothstep(layered.seaLevel, layered.seaLevel + layered.coastWidth, base);
			NestedPoint point{ cs.x[i], base * (1.0 - share), {} };
			if (land > 0.0) {
				NestedCurve mCurve;
				mCurve.cubic = ms.cubic;
				for (size_t j = 0; j < ms.x.size(); j++) {
					const double amplitude = ms.y[j];
					NestedPoint mPoint{ ms.x[j], base * (1.0 - share), {} };
					if (amplitude > 0.0) {
						NestedCurve wCurve;
						wCurve.cubic = ps.cubic;
						for (size_t k = 0; k < ps.x.size(); k++) {
							wCurve.points.push_back({ ps.x[k], base * (1.0 - share) + land * amplitude * ps.y[k] * share, {} });
						}
						mPoint.sub.push_back(std::move(wCurve));
					}
					mCurve.points.push_back(std::move(mPoint));
				}
				point.sub.push_back(std::move(mCurve));
			}
			tree.root.points.push_back(std::move(point));
		}
		return tree;
	}

	const NestedCurve& RootFor(const TerrainConfig& t, NestedCurve& scratch)
	{
		if (!t.nested.root.points.empty()) {
			return t.nested.root;
		}
		scratch = FromLayered(t).root;
		return scratch;
	}

	Flat Flatten(const NestedCurve& root)
	{
		Flat flat;
		Add(root, flat);
		return flat;
	}

	bool Equal(const NestedCurve& a, const NestedCurve& b)
	{
		if (a.cubic != b.cubic || a.points.size() != b.points.size()) {
			return false;
		}
		for (size_t i = 0; i < a.points.size(); i++) {
			const NestedPoint& p = a.points[i];
			const NestedPoint& q = b.points[i];
			if (p.x != q.x || p.sub.size() != q.sub.size() || (p.sub.empty() ? p.y != q.y : !Equal(p.sub[0], q.sub[0]))) {
				return false;
			}
		}
		return true;
	}

	bool ValidPath(const NestedCurve& root, std::vector<int>& path)
	{
		const NestedCurve* curve = &root;
		for (size_t i = 0; i < path.size(); i++) {
			const int index = path[i];
			if (i + 1 >= static_cast<size_t>(maxDepth) || index < 0 || index >= static_cast<int>(curve->points.size())
				|| curve->points[index].sub.empty()) {
				path.resize(i);
				return false;
			}
			curve = &curve->points[index].sub[0];
		}
		return true;
	}

	std::string Problem(const NestedCurve& root)
	{
		int wCurves = 0;
		std::string problem = CurveProblem(root, 0, wCurves);
		if (problem.empty() && wCurves > maxWCurves) {
			problem = std::to_string(wCurves) + " curves over weirdness, at most " + std::to_string(maxWCurves);
		}
		return problem;
	}
}
