// Terrain height from the three terrain noise values, shared by height_compute.shader and biome_compute.shader,
// so the biomes see exactly the height of the terrain.
// The file including this one provides: float splines[3 * SPLINE_SAMPLES] (in a buffer, filled by SampleSpline in
// SplineSampling.cpp), the nested curves nestedCurves / nestedKnots (world_input.glsl), uniform int mode and uniform vec3 layered
// (seaLevel, coastWidth, mountainShare of TerrainConfig).

const int SPLINE_SAMPLES = 1024;
const int MODE_NOISE = 0, MODE_LINEAR = 1, MODE_SPLINE = 2, MODE_LAYERED = 3, MODE_NESTED = 4;

// A value outside [-1, 1] takes the end of the table
float evaluateSpline(int index, float value)
{
	float position = (clamp(value, -1.0, 1.0) + 1.0) * 0.5 * float(SPLINE_SAMPLES - 1);
	int sampleIndex = min(int(position), SPLINE_SAMPLES - 2);
	int first = index * SPLINE_SAMPLES + sampleIndex;
	return mix(splines[first], splines[first + 1], position - float(sampleIndex));
}

// Nested curves: the rule of nested::Evaluate (NestedSpline.cpp) with EvaluateSpline (SplineSampling.cpp) — a change in one needs
// the same change in the other; --gpu-check (section nested:) compares them. GLSL has no recursion, so each level has its own function,
// and a curve evaluates only the knots its segment needs: 2 for straight segments, up to 4 for PCHIP (slopes from the neighbours).

// Segment of curve holding t as EvaluateSpline picks it: the first one whose end is not below t
int nestedSegment(NestedCurve curve, float t)
{
	int segment = 0;
	while (segment + 2 < curve.count && t > nestedKnots[curve.firstKnot + segment + 1].x) {
		segment++;
	}
	return segment;
}

// Knots lo..hi whose values the segment needs
void nestedWindow(NestedCurve curve, int segment, out int lo, out int hi)
{
	lo = curve.cubic != 0 ? max(segment - 1, 0) : segment;
	hi = curve.cubic != 0 ? min(segment + 2, curve.count - 1) : segment + 1;
}

// Secant between knots i and i + 1; v[k - lo] = value of knot k
float nestedSecant(int first, int i, float v[4], int lo)
{
	return (v[i + 1 - lo] - v[i - lo]) / (nestedKnots[first + i + 1].x - nestedKnots[first + i].x);
}

// End slope of MonotoneSlopes: a one-sided three-point estimate, limited so that the end piece stays monotone
float nestedEndSlope(float h0, float h1, float d0, float d1)
{
	float s = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
	if (s * d0 <= 0.0) {
		return 0.0;
	}
	if (d0 * d1 <= 0.0 && abs(s) > 3.0 * abs(d0)) {
		s = 3.0 * d0;
	}
	return s;
}

// PCHIP slope (Fritsch-Carlson, MonotoneSlopes) at knot i of curve
float nestedSlope(NestedCurve curve, int i, float v[4], int lo)
{
	int first = curve.firstKnot, n = curve.count;
	if (i == 0) {
		float h0 = nestedKnots[first + 1].x - nestedKnots[first].x, h1 = nestedKnots[first + 2].x - nestedKnots[first + 1].x;
		return nestedEndSlope(h0, h1, nestedSecant(first, 0, v, lo), nestedSecant(first, 1, v, lo));
	}
	if (i == n - 1) {
		float h0 = nestedKnots[first + n - 1].x - nestedKnots[first + n - 2].x, h1 = nestedKnots[first + n - 2].x - nestedKnots[first + n - 3].x;
		return nestedEndSlope(h0, h1, nestedSecant(first, n - 2, v, lo), nestedSecant(first, n - 3, v, lo));
	}
	float before = nestedSecant(first, i - 1, v, lo), after = nestedSecant(first, i, v, lo);
	if (before * after > 0.0) {
		float h = nestedKnots[first + i + 1].x - nestedKnots[first + i].x, hBefore = nestedKnots[first + i].x - nestedKnots[first + i - 1].x;
		float w1 = 2.0 * h + hBefore, w2 = h + 2.0 * hBefore;
		return (w1 + w2) / (w1 / before + w2 / after);
	}
	return 0.0;
}

// Value of curve at t (in its segment) from the values of the knots of its window
float nestedInterpolate(NestedCurve curve, int segment, float t, float v[4], int lo)
{
	int first = curve.firstKnot;
	float x0 = nestedKnots[first + segment].x;
	float h = nestedKnots[first + segment + 1].x - x0;
	float s = (t - x0) / h;
	float y0 = v[segment - lo], y1 = v[segment + 1 - lo];
	if (curve.cubic == 0) {
		return y0 + s * (y1 - y0);
	}
	float d0 = nestedSlope(curve, segment, v, lo), d1 = nestedSlope(curve, segment + 1, v, lo);
	float s2 = s * s, s3 = s2 * s;
	return (2.0 * s3 - 3.0 * s2 + 1.0) * y0 + (s3 - 2.0 * s2 + s) * h * d0 + (-2.0 * s3 + 3.0 * s2) * y1 + (s3 - s2) * h * d1;
}

// Curve over weirdness: constants only
float nestedW(int index, float weirdness)
{
	NestedCurve curve = nestedCurves[index];
	float t = clamp(weirdness, -1.0, 1.0);
	int segment = nestedSegment(curve, t);
	int lo, hi;
	nestedWindow(curve, segment, lo, hi);
	float v[4] = float[4](0.0, 0.0, 0.0, 0.0);
	for (int k = lo; k <= hi; k++) {
		v[k - lo] = nestedKnots[curve.firstKnot + k].value;
	}
	return nestedInterpolate(curve, segment, t, v, lo);
}

// Curve over mountainousness: a knot is a constant or a curve over weirdness
float nestedM(int index, float mountainousness, float weirdness)
{
	NestedCurve curve = nestedCurves[index];
	float t = clamp(mountainousness, -1.0, 1.0);
	int segment = nestedSegment(curve, t);
	int lo, hi;
	nestedWindow(curve, segment, lo, hi);
	float v[4] = float[4](0.0, 0.0, 0.0, 0.0);
	for (int k = lo; k <= hi; k++) {
		NestedKnot knot = nestedKnots[curve.firstKnot + k];
		v[k - lo] = knot.child < 0 ? knot.value : nestedW(knot.child, weirdness);
	}
	return nestedInterpolate(curve, segment, t, v, lo);
}

// The root, curve 0, over continentalness: a knot is a constant or a curve over mountainousness
float nestedC(float continentalness, float mountainousness, float weirdness)
{
	NestedCurve curve = nestedCurves[0];
	float t = clamp(continentalness, -1.0, 1.0);
	int segment = nestedSegment(curve, t);
	int lo, hi;
	nestedWindow(curve, segment, lo, hi);
	float v[4] = float[4](0.0, 0.0, 0.0, 0.0);
	for (int k = lo; k <= hi; k++) {
		NestedKnot knot = nestedKnots[curve.firstKnot + k];
		v[k - lo] = knot.child < 0 ? knot.value : nestedM(knot.child, mountainousness, weirdness);
	}
	return nestedInterpolate(curve, segment, t, v, lo);
}

// Height in [0, 1] for MODE_LINEAR, MODE_SPLINE, MODE_LAYERED and MODE_NESTED
float terrainHeight(float continentalness, float mountainousness, float weirdness)
{
	if (mode == MODE_NESTED) {
		return nestedC(continentalness, mountainousness, weirdness);
	}
	if (mode == MODE_LINEAR) {
		return (continentalness + 1.0) / 2.0 * ((mountainousness + 1.0) / 2.0) * (1.0 - (weirdness + 1.0) / 2.0);
	}
	if (mode == MODE_LAYERED) {
		// Spline 2 holds peaksSpline here; see TerrainConfig::seaLevel for the formula
		float base = evaluateSpline(0, continentalness);
		float land = smoothstep(layered.x, layered.x + layered.y, base);
		float relief = evaluateSpline(1, mountainousness) * evaluateSpline(2, weirdness);
		return base * (1.0 - layered.z) + land * relief * layered.z;
	}
	return evaluateSpline(0, continentalness) * evaluateSpline(1, mountainousness) * evaluateSpline(2, weirdness);
}
