// Fractal simplex noise configured by noise::NoiseConfigParameters (the only implementation; parameters come from GpuHeightGenerator::SetNoise).
// The including file provides: NoiseParams noises[], uint permutations[] (256 entries per noise) and uniform ivec2 islandSize
// (size of the map the island fills, from map position (0, 0)).

int permAt(int table, int i)
{
	return int(permutations[table * 256 + (i & 255)]);
}

#include "simplex.glsl"

float islandDistance(int type, float nx, float ny)
{
	const float PI = 3.14159265358979;
	float nx2 = nx * nx, ny2 = ny * ny;
	switch (type) {
	case 0: return sqrt(nx2 + ny2);							// cone
	case 1: return max(abs(nx), abs(ny));					// diagonal
	case 2: return min(1.0, (nx2 + ny2) / sqrt(2.0));		// euclidean squared
	case 3: return 1.0 - (1.0 - nx2) * (1.0 - ny2);			// square bump
	case 4: return sqrt(nx2 + ny2 + 0.25);					// hyperboloid
	case 5: return sqrt(nx2 * nx2 + ny2 * ny2);				// squircle
	case 6: return 1.0 - cos(nx * PI / 2.0) * cos(ny * PI / 2.0);	// trig
	}
	return 0.0;
}

// Two-component 4 octave noise in [-1, 1] that moves the coordinates in domain warping; the offsets keep its components apart
vec2 warpOffset(int index, vec2 q)
{
	vec2 sum = vec2(0.0);
	float amplitude = 1.0, frequency = 1.0, divider = 0.0;
	for (int i = 0; i < 4; i++) {
		vec2 a = (q + vec2(17.3, 41.9)) * frequency, b = (q + vec2(-63.1, 5.7)) * frequency;
		sum += vec2(simplex2(index, a.x, a.y), simplex2(index, b.x, b.y)) * amplitude;
		divider += amplitude;
		amplitude *= 0.5;
		frequency *= 2.0;
	}
	return sum / divider;
}

// Sum of the octaves before contrast; p in noise space (map position / resolution * scale + offset).
// Domain warping moves p first; slope erosion divides every octave by 1 + slopeErosion * |gradient of the octaves below|^2
float fractalSum(int index, NoiseParams n, vec2 p, out float divider)
{
	if (n.warpStrength != 0.0) {
		p += n.warpStrength * warpOffset(index, p * n.warpScale);
	}
	float amplitude = 1.0, frequency = 1.0, elevation = 0.0;
	vec2 slope = vec2(0.0);
	divider = 0.0;
	for (int i = 0; i < n.octaves; i++) {
		if (n.slopeErosion > 0.0) {
			vec3 v = simplex2Grad(index, p.x * frequency, p.y * frequency);
			// Derivative with respect to p of this octave's contribution
			slope += v.yz * frequency * amplitude;
			elevation += v.x * amplitude / (1.0 + n.slopeErosion * dot(slope, slope));
		}
		else {
			elevation += simplex2(index, p.x * frequency, p.y * frequency) * amplitude;
		}
		divider += amplitude;
		amplitude *= n.persistence;
		frequency *= n.lacunarity;
	}
	return elevation;
}

float evaluateNoise(int index, float x, float y)
{
	const float TAU = 6.28318530717959;
	NoiseParams n = noises[index];

	float amplitude = 1.0, frequency = 1.0, elevation = 0.0, divider = 0.0;
	if (n.symmetrical == 0 && (n.warpStrength != 0.0 || n.slopeErosion > 0.0)) {
		elevation = fractalSum(index, n, vec2(x / float(n.resolution) * n.scale + n.xoffset, y / float(n.resolution) * n.scale + n.yoffset), divider);
	}
	else {
		// Without warping and slope erosion: the original loop, so existing worlds stay bit for bit the same
		for (int i = 0; i < n.octaves; i++) {
			if (n.symmetrical != 0) {
				float anglex = TAU * (x / float(n.resolution));
				float angley = TAU * (y / float(n.resolution));
				elevation += simplex4(index, cos(anglex) / TAU * n.scale * frequency + n.xoffset, sin(anglex) / TAU * n.scale * frequency + n.xoffset,
					cos(angley) / TAU * n.scale * frequency + n.yoffset, sin(angley) / TAU * n.scale * frequency + n.yoffset) * amplitude;
			}
			else {
				elevation += simplex2(index, (x / float(n.resolution) * n.scale + n.xoffset) * frequency,
					(y / float(n.resolution) * n.scale + n.yoffset) * frequency) * amplitude;
			}
			divider += amplitude;
			amplitude *= n.persistence;
			frequency *= n.lacunarity;
		}
	}
	elevation = clamp(elevation * n.contrast / divider, -1.0, 1.0);

	// Options: 0 refit all, 1 flatten negatives, 2 revert negatives, 3 nothing
	if (n.option == 0) {
		elevation = (elevation + 1.0) / 2.0;
	}
	else if (elevation < 0.0 && n.option == 1) {
		elevation = 0.0;
	}
	else if (elevation < 0.0 && n.option == 2) {
		elevation = -(elevation * n.revertGain);
	}

	if (n.ridge != 0) {
		elevation = n.ridgeOffset - abs(n.ridgeGain * abs(elevation) - n.ridgeGain + 1.0);
	}

	if (n.island != 0) {
		float nx = float(int(x) * 2) / float(islandSize.x) - 1.0;
		float ny = float(int(y) * 2) / float(islandSize.y) - 1.0;
		elevation = abs(mix(elevation, 1.0 - islandDistance(n.islandType, nx, ny), n.mixPower));
	}

	// pow in GLSL is undefined for 0 and for negative bases, so the sign and 0 are handled here
	if (n.redistribution != 1.0) {
		if (elevation > 0.0) {
			elevation = pow(elevation, n.redistribution);
		}
		else if (elevation < 0.0) {
			elevation = -pow(-elevation, n.redistribution);
		}
		else if (n.redistribution == 0.0) {
			elevation = 1.0;
		}
	}

	// Terraces: every 1 / terraceSteps of height is a flat ledge rising to the next one; the riser sits in the middle of the step
	// and narrows as sharpness grows. Continuous, and keeps the range: values on step boundaries do not move
	if (n.terraceSteps > 0) {
		float scaled = elevation * float(n.terraceSteps);
		float level = floor(scaled);
		float halfRiser = 0.5 * (1.0 - clamp(n.terraceSharpness, 0.0, 0.99));
		elevation = (level + smoothstep(0.5 - halfRiser, 0.5 + halfRiser, scaled - level)) / float(n.terraceSteps);
	}
	return elevation;
}
