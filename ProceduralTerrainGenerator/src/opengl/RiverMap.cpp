#include "RiverMap.h"

#include <cstddef>

RiverMap::~RiverMap()
{
	if (waterVao) {
		glDeleteVertexArrays(1, &waterVao);
		glDeleteBuffers(1, &waterBuffer);
	}
}

void RiverMap::Upload(const hydrology::Network& network, const std::vector<hydrology::WaterVertex>& water)
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

	if (!waterVao) {
		glCreateVertexArrays(1, &waterVao);
		glCreateBuffers(1, &waterBuffer);
		constexpr GLsizei stride = sizeof(hydrology::WaterVertex);
		glVertexArrayVertexBuffer(waterVao, 0, waterBuffer, 0, stride);
		const GLint sizes[] = { 2, 1, 2 };
		const GLuint offsets[] = { offsetof(hydrology::WaterVertex, x), offsetof(hydrology::WaterVertex, level), offsetof(hydrology::WaterVertex, flowX) };
		for (GLuint a = 0; a < 3; a++) {
			glEnableVertexArrayAttrib(waterVao, a);
			glVertexArrayAttribFormat(waterVao, a, sizes[a], GL_FLOAT, GL_FALSE, offsets[a]);
			glVertexArrayAttribBinding(waterVao, a, 0);
		}
	}
	//The size changes with every network, so the buffer gets new storage
	glNamedBufferData(waterBuffer, static_cast<GLsizeiptr>(water.size() * sizeof(hydrology::WaterVertex)), water.empty() ? nullptr : water.data(), GL_STATIC_DRAW);
	waterVertices = static_cast<GLsizei>(water.size());
}

void RiverMap::Clear()
{
	rivers.reset();
	levels.reset();
	waterVertices = 0;
}

void RiverMap::Bind() const
{
	if (rivers) {
		rivers->Bind(riverUnit);
		levels->Bind(levelUnit);
	}
}
