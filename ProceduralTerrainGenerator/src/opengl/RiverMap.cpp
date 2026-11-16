#include "RiverMap.h"

void RiverMap::Upload(const hydrology::Network& network)
{
	const int width = network.rivers.GetWidth(), height = network.rivers.GetHeight();
	if (width == 0 || height == 0) {
		Clear();
		return;
	}
	if (!rivers || rivers->GetWidth() != width || rivers->GetHeight() != height) {
		rivers = std::make_unique<TextureClass>(width, height, GL_RGBA32F);
		levels = std::make_unique<TextureClass>(width, height, GL_RG32F);
	}
	static_assert(sizeof(hydrology::RiverTexel) == 4 * sizeof(float) && sizeof(hydrology::LevelTexel) == 2 * sizeof(float));
	rivers->Upload(network.rivers.Data(), GL_RGBA, GL_FLOAT);
	levels->Upload(network.levels.Data(), GL_RG, GL_FLOAT);
	origin = network.origin;
	cellSize = network.cellSize;
}

void RiverMap::Clear()
{
	rivers.reset();
	levels.reset();
}

void RiverMap::Bind() const
{
	if (rivers) {
		rivers->Bind(riverUnit);
		levels->Bind(levelUnit);
	}
}
