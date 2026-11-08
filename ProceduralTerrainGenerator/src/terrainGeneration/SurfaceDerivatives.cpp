#include "SurfaceDerivatives.h"

#include <algorithm>
#include <cmath>

namespace surface
{
	namespace {
		//Change of height per world unit between the neighbours of i (one-sided at the edges)
		//@param at - height at an index along the axis
		template<typename F>
		float FirstDifference(F at, int i, int count, float texelSize)
		{
			const int low = std::max(i - 1, 0), high = std::min(i + 1, count - 1);
			if (high == low) {
				return 0.0f;
			}
			return (at(high) - at(low)) / (static_cast<float>(high - low) * texelSize);
		}

		//Second difference in height units (not divided by the texel size squared)
		template<typename F>
		float SecondDifference(F at, int i, int count)
		{
			if (count < 3) {
				return 0.0f;
			}
			const int center = std::clamp(i, 1, count - 2);
			return at(center - 1) - 2.0f * at(center) + at(center + 1);
		}
	}

	DerivativeMap Compute(const HeightField& height, float texelSize)
	{
		const int width = height.GetWidth(), rows = height.GetHeight();
		DerivativeMap result(width, rows);
		for (int y = 0; y < rows; y++) {
			const auto alongX = [&](int i) { return height.At(i, y); };
			for (int x = 0; x < width; x++) {
				const auto alongY = [&](int i) { return height.At(x, i); };
				Derivatives& d = result.At(x, y);
				d.dx = FirstDifference(alongX, x, width, texelSize);
				d.dy = FirstDifference(alongY, y, rows, texelSize);
				d.laplacian = (SecondDifference(alongX, x, width) + SecondDifference(alongY, y, rows)) / (texelSize * texelSize);
			}
		}
		return result;
	}

	glm::vec3 Normal(const Derivatives& d, float heightScale)
	{
		return glm::normalize(glm::vec3(-d.dx * heightScale, 1.0f, -d.dy * heightScale));
	}

	float Slope(const Derivatives& d, float heightScale)
	{
		return std::atan(std::hypot(d.dx, d.dy) * std::abs(heightScale));
	}

	float Curvature(const Derivatives& d, float heightScale)
	{
		return d.laplacian * heightScale;
	}
}
