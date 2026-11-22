#include "Sculpt.h"

#include <algorithm>
#include <cmath>

#include "glm/gtc/matrix_transform.hpp"

namespace sculpt {
	namespace {
		//Integer hash to [-1, 1]; uint arithmetic wraps the same way in GLSL
		float Hash(int x, int y, uint32_t seed)
		{
			uint32_t n = static_cast<uint32_t>(x) * 0x27d4eb2du ^ static_cast<uint32_t>(y) * 0x165667b1u ^ seed * 0x9e3779b9u;
			n = (n ^ (n >> 15)) * 0x2c1b3c6du;
			n = (n ^ (n >> 12)) * 0x297a2d39u;
			n ^= n >> 15;
			return static_cast<float>(n & 0xFFFFFFu) / 16777215.0f * 2.0f - 1.0f;
		}

		float ValueNoise(glm::vec2 p, uint32_t seed)
		{
			const glm::vec2 i = glm::floor(p);
			const glm::vec2 f = p - i;
			const glm::vec2 u = f * f * (3.0f - 2.0f * f);
			const int x = static_cast<int>(i.x), y = static_cast<int>(i.y);
			const float a = Hash(x, y, seed), b = Hash(x + 1, y, seed);
			const float c = Hash(x, y + 1, seed), d = Hash(x + 1, y + 1, seed);
			return glm::mix(glm::mix(a, b, u.x), glm::mix(c, d, u.x), u.y);
		}

		glm::vec2 TexelOrigin(glm::ivec2 mapSize)
		{
			//World XZ (before modelScale) of the centre of texel (0, 0)
			return (-glm::vec2(mapSize) * 0.5f + 0.5f) * worldUnitsPerTexel;
		}
	}

	const char* BrushName(Brush brush)
	{
		switch (brush) {
		case Brush::RAISE: return "Raise";
		case Brush::LOWER: return "Lower";
		case Brush::SMOOTH: return "Smooth";
		case Brush::FLATTEN: return "Flatten";
		case Brush::NOISE: return "Noise";
		}
		return "";
	}

	float Falloff(float distance, float radius, float hardness)
	{
		if (distance >= radius) {
			return 0.0f;
		}
		if (hardness >= 1.0f) {
			return 1.0f;
		}
		const float edge = std::max(hardness, 0.0f) * radius;
		const float t = std::clamp((distance - edge) / (radius - edge), 0.0f, 1.0f);
		return 1.0f - t * t * (3.0f - 2.0f * t);
	}

	glm::ivec4 Area(const Dab& dab, float radius, glm::ivec2 mapSize)
	{
		const int lowX = std::max(0, static_cast<int>(std::floor(dab.center.x - radius)));
		const int lowY = std::max(0, static_cast<int>(std::floor(dab.center.y - radius)));
		const int highX = std::min(mapSize.x - 1, static_cast<int>(std::ceil(dab.center.x + radius)));
		const int highY = std::min(mapSize.y - 1, static_cast<int>(std::ceil(dab.center.y + radius)));
		if (highX < lowX || highY < lowY) {
			return glm::ivec4(0);
		}
		return glm::ivec4(lowX, lowY, highX - lowX + 1, highY - lowY + 1);
	}

	int SmoothSpacing(float radius)
	{
		return std::max(1, static_cast<int>(std::round(radius / 16.0f)));
	}

	float NoiseAt(glm::vec2 texel, float scale, uint32_t seed)
	{
		const glm::vec2 p = texel / std::max(scale, 1.0f);
		return (2.0f / 3.0f) * ValueNoise(p, seed) + (1.0f / 3.0f) * ValueNoise(p * 2.0f + 17.0f, seed + 1u);
	}

	void Apply(HeightField& units, const BrushConfig& config, const Dab& dab)
	{
		const glm::ivec2 size(units.GetWidth(), units.GetHeight());
		const glm::ivec4 area = Area(dab, config.radius, size);
		if (area.z == 0) {
			return;
		}
		const HeightField source = units;
		auto at = [&](int x, int y) { return source.At(std::clamp(x, 0, size.x - 1), std::clamp(y, 0, size.y - 1)); };
		const int spacing = SmoothSpacing(config.radius);
		for (int y = area.y; y < area.y + area.w; y++) {
			for (int x = area.x; x < area.x + area.z; x++) {
				const float w = Falloff(glm::distance(glm::vec2(x, y), dab.center), config.radius, config.hardness);
				if (w <= 0.0f) {
					continue;
				}
				float h = source.At(x, y);
				switch (config.brush) {
				case Brush::RAISE:
					h += dab.amount * w;
					break;
				case Brush::LOWER:
					h -= dab.amount * w;
					break;
				case Brush::NOISE:
					h += dab.amount * w * NoiseAt(glm::vec2(x, y), config.noiseScale, dab.seed);
					break;
				case Brush::SMOOTH: {
					float sum = 0.0f;
					for (int dy = -1; dy <= 1; dy++) {
						for (int dx = -1; dx <= 1; dx++) {
							sum += at(x + dx * spacing, y + dy * spacing);
						}
					}
					h += (sum / 9.0f - h) * std::min(dab.amount * w, 1.0f);
					break;
				}
				case Brush::FLATTEN:
					h += (dab.target - h) * std::min(dab.amount * w, 1.0f);
					break;
				}
				units.At(x, y) = std::clamp(h, -dab.limit, dab.limit);
			}
		}
	}

	float StrokeLimit(float metresPerUnit, float largestMetres, float metresPerSecond)
	{
		const float needed = (std::abs(largestMetres) + std::abs(metresPerSecond) * strokeHeadroomSeconds) / std::max(metresPerUnit, 1e-6f);
		return std::max(1.5f, needed);
	}

	std::optional<glm::vec2> PickTexel(glm::vec2 windowPixel, float depth, glm::ivec4 viewport, const glm::mat4& viewProjection, float modelScale,
		glm::ivec2 mapSize)
	{
		if (!(depth < 1.0f) || modelScale <= 0.0f) {
			return std::nullopt;
		}
		const glm::vec3 world = glm::unProject(glm::vec3(windowPixel, depth), glm::mat4(1.0f), viewProjection, glm::vec4(viewport));
		const glm::vec2 texel = (glm::vec2(world.x, world.z) / modelScale - TexelOrigin(mapSize)) / worldUnitsPerTexel;
		const glm::vec2 last = glm::vec2(mapSize) - 0.5f;
		if (!(texel.x >= -0.5f && texel.y >= -0.5f && texel.x <= last.x && texel.y <= last.y)) {
			return std::nullopt;
		}
		return texel;
	}

	glm::vec3 TexelToWorld(glm::vec2 texel, float heightUnits, float heightScale, float modelScale, glm::ivec2 mapSize)
	{
		const glm::vec2 xz = TexelOrigin(mapSize) + texel * worldUnitsPerTexel;
		return modelScale * glm::vec3(xz.x, heightUnits * heightScale, xz.y);
	}
}
