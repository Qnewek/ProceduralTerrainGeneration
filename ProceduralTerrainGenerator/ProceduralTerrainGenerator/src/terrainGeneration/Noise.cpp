#include "Noise.h"
#include "glm/glm.hpp"
#include "glm/gtc/noise.hpp"
namespace noise
{
	float* GetNoiseMap(int mapWidth, int mapHeigth, float scale, int octaves, float persistance, float lacunarity, float offsetX, float offsetY)
	{
		float* noiseMap = new float[mapWidth * mapHeigth];

		if (scale <= 0) {
			scale = 0.0001f;
		}

		for (int y = 0; y < mapHeigth; y++)
		{
			for (int x = 0; x < mapWidth; x++)
			{
				//float amplitude = 1;
				//float frequency = 1;
				//float noiseHeight = 0;

				//for (int i = 0; i < octaves; i++)
				//{
					float sampleX = x / scale;
					float sampleY = y / scale;

					float perlinValue = glm::perlin(glm::vec2(sampleX, sampleY));

					//noiseHeight += perlinValue * amplitude;

					//amplitude *= persistance;
					//frequency *= lacunarity;
				//}

				noiseMap[y * mapWidth + x] = perlinValue;
			}
		}
	}
}