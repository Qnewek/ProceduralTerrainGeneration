// Terrain height from the three terrain noise values, shared by height_compute.shader and biome_compute.shader,
// so the biomes see exactly the height of the terrain.
// The file including this one provides: float splines[3 * SPLINE_SAMPLES] (in a buffer, filled by SampleSpline in
// SplineSampling.cpp), uniform int mode and uniform vec3 layered (seaLevel, coastWidth, mountainShare of TerrainConfig).

const int SPLINE_SAMPLES = 1024;
const int MODE_NOISE = 0, MODE_LINEAR = 1, MODE_SPLINE = 2, MODE_LAYERED = 3;

// A value outside [-1, 1] takes the end of the table
float evaluateSpline(int index, float value)
{
	float position = (clamp(value, -1.0, 1.0) + 1.0) * 0.5 * float(SPLINE_SAMPLES - 1);
	int sampleIndex = min(int(position), SPLINE_SAMPLES - 2);
	int first = index * SPLINE_SAMPLES + sampleIndex;
	return mix(splines[first], splines[first + 1], position - float(sampleIndex));
}

// Height in [0, 1] for MODE_LINEAR, MODE_SPLINE and MODE_LAYERED
float terrainHeight(float continentalness, float mountainousness, float weirdness)
{
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
