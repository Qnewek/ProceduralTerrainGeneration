#version 450 core

// Gradient and Laplacian of a height map; GLSL version of surface::Compute (SurfaceDerivatives.cpp).
// A change has to be repeated there; ProceduralTerrainGenerator.exe --gpu-check compares both.
// Output: x = dh/dx, y = dh/dy per world unit, z = Laplacian per world unit squared, w = 0.

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba32f, binding = 0) uniform writeonly image2D surfaceMap;

uniform sampler2D heightMap;
uniform float texelSize;	// distance between neighbouring texels in world units

float heightAt(int x, int y)
{
	return texelFetch(heightMap, ivec2(x, y), 0).r;
}

void main()
{
	ivec2 size = textureSize(heightMap, 0);
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(p, size))) {
		return;
	}

	// One-sided differences at the edges
	ivec2 low = max(p - 1, ivec2(0));
	ivec2 high = min(p + 1, size - 1);
	float dx = high.x == low.x ? 0.0 : (heightAt(high.x, p.y) - heightAt(low.x, p.y)) / (float(high.x - low.x) * texelSize);
	float dy = high.y == low.y ? 0.0 : (heightAt(p.x, high.y) - heightAt(p.x, low.y)) / (float(high.y - low.y) * texelSize);

	// Second differences at the nearest texel that has both neighbours
	float d2x = 0.0, d2y = 0.0;
	if (size.x >= 3) {
		int c = clamp(p.x, 1, size.x - 2);
		d2x = heightAt(c - 1, p.y) - 2.0 * heightAt(c, p.y) + heightAt(c + 1, p.y);
	}
	if (size.y >= 3) {
		int c = clamp(p.y, 1, size.y - 2);
		d2y = heightAt(p.x, c - 1) - 2.0 * heightAt(p.x, c) + heightAt(p.x, c + 1);
	}

	imageStore(surfaceMap, p, vec4(dx, dy, (d2x + d2y) / (texelSize * texelSize), 0.0));
}
