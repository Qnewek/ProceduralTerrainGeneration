#pragma once
namespace noise
{
	float* GetNoiseMap(int mapWidth, int mapHeigth, float scale, int octaves, float persistance, float lacunarity, float offsetX, float offsetY);
}
