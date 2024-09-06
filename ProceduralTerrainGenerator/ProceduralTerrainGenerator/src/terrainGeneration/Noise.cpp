#include "Noise.h"
#include "glm/glm.hpp"
#include "math.h"

#define PI 3.14159265

namespace noise
{
	void getNoiseMap(float* noiseMap, int mapWidth, int mapHeigth, float scale, int octaves, float constrast, Options option)
	{
		float amplitude;
		float frequency;
		float noiseHeight;
		for (int y = 0; y < mapHeigth; y++)
		{
			for (int x = 0; x < mapWidth; x++)
			{
				amplitude = 1.0f;
				frequency = 1.0f;
				noiseHeight = 0.0f;

				for (int i = 0; i < octaves; i++)
				{
					noiseHeight += perlin(x / scale * frequency, y / scale * frequency) * amplitude;

					amplitude *= 0.5f;
					frequency *= 2.0f;
				}

				noiseHeight *= constrast;

				if (noiseHeight > 1.0f)
				{
					noiseHeight = 1.0f;
				}
				else if (noiseHeight < -1.0f)
				{
					noiseHeight = -1.0f;
				}

				if (option == Options::REFIT_BASIC)
				{
					noiseHeight = (noiseHeight + 1.0f) / 2.0f;
				}
				else if (noiseHeight < 0.0f)
				{
					if (option == Options::FLATTEN_NEGATIVES)
					{
						noiseHeight = 0.0f;
					}
					else if (option == Options::REVERT_NEGATIVES)
					{
						noiseHeight = -noiseHeight;
					}
				}

				noiseMap[y * mapWidth + x] = noiseHeight;
			}
		}
	}

	vec2 randomGradient(int ix, int iy) {
		const unsigned w = 8 * sizeof(unsigned);
		const unsigned s = w >> 1;
		unsigned a = ix, b = iy;

		a *= 3284157443;
		b ^= a << s | a >> w - s;
		b *= 1911520717;

		a ^= b << s | b >> w - s;
		a *= 2048419325;
		float random = a * (PI / ~(~0u >> 1));

		vec2 v;
		v.x = sin(random);
		v.y = cos(random);

		return v;
	}

	float dotGridGradient(int ix, int iy, float x, float y) {
		vec2 gradient = randomGradient(ix, iy);

		float dx = x - (float)ix;
		float dy = y - (float)iy;

		return (dx * gradient.x + dy * gradient.y);
	}
	float interpolate(float a0, float a1, float w)
	{
		return (a1 - a0) * (3.0 - w * 2.0) * w * w + a0;
	}

	float perlin(float x, float y) {
		int x0 = (int)x;
		int y0 = (int)y;
		int x1 = x0 + 1;
		int y1 = y0 + 1;

		float sx = x - (float)x0;
		float sy = y - (float)y0;

		float n0 = dotGridGradient(x0, y0, x, y);
		float n1 = dotGridGradient(x1, y0, x, y);
		float ix0 = interpolate(n0, n1, sx);

		n0 = dotGridGradient(x0, y1, x, y);
		n1 = dotGridGradient(x1, y1, x, y);
		float ix1 = interpolate(n0, n1, sx);

		float value = interpolate(ix0, ix1, sy);

		return value;
	}

}