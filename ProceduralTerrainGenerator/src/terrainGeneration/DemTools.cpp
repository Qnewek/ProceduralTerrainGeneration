#include "DemTools.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dem {
	int FillNoData(HeightField& map, const std::vector<uint8_t>& missing)
	{
		const int width = map.GetWidth(), height = map.GetHeight();
		std::vector<uint8_t> known(map.Size());
		std::vector<size_t> holes;
		for (size_t i = 0; i < map.Size(); i++) {
			known[i] = missing[i] == 0;
			if (!known[i]) {
				holes.push_back(i);
			}
		}
		if (holes.empty()) {
			return 0;
		}
		if (holes.size() == map.Size()) {
			return -1;
		}
		const int filled = static_cast<int>(holes.size());
		//Each wave fills the holes touching known texels, from the values known before it, so the result does not depend on the order.
		//Breadth-first: the next wave is the unknown neighbours of the texels just filled, so every hole is visited once (linear time)
		auto forNeighbours = [&](size_t i, auto&& visit) {
			const int x = static_cast<int>(i % width), y = static_cast<int>(i / width);
			if (x > 0) { visit(i - 1); }
			if (x + 1 < width) { visit(i + 1); }
			if (y > 0) { visit(i - width); }
			if (y + 1 < height) { visit(i + width); }
		};
		std::vector<uint8_t> queued(map.Size(), 0);
		std::vector<size_t> frontier;
		for (size_t i : holes) {
			bool touchesKnown = false;
			forNeighbours(i, [&](size_t j) { touchesKnown = touchesKnown || known[j]; });
			if (touchesKnown) {
				frontier.push_back(i);
				queued[i] = 1;
			}
		}
		holes = {};
		std::vector<float> values;
		std::vector<size_t> next;
		while (!frontier.empty()) {
			values.resize(frontier.size());
			for (size_t k = 0; k < frontier.size(); k++) {
				double sum = 0.0;
				int count = 0;
				forNeighbours(frontier[k], [&](size_t j) {
					if (known[j]) {
						sum += map[j];
						count++;
					}
				});
				values[k] = static_cast<float>(sum / count);
			}
			next.clear();
			for (size_t k = 0; k < frontier.size(); k++) {
				map[frontier[k]] = values[k];
				known[frontier[k]] = 1;
			}
			for (size_t i : frontier) {
				forNeighbours(i, [&](size_t j) {
					if (!known[j] && !queued[j]) {
						queued[j] = 1;
						next.push_back(j);
					}
				});
			}
			std::swap(frontier, next);
		}
		return filled;
	}

	HeightField Crop(const HeightField& map, glm::ivec2 first, glm::ivec2 size)
	{
		HeightField part(size.x, size.y);
		for (int y = 0; y < size.y; y++) {
			for (int x = 0; x < size.x; x++) {
				part.At(x, y) = map.At(first.x + x, first.y + y);
			}
		}
		return part;
	}

	HeightField Downsample(const HeightField& map, int factor)
	{
		factor = std::max(factor, 1);
		const int width = (map.GetWidth() + factor - 1) / factor, height = (map.GetHeight() + factor - 1) / factor;
		HeightField small(width, height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				double sum = 0.0;
				int count = 0;
				const int lastY = std::min((y + 1) * factor, map.GetHeight()), lastX = std::min((x + 1) * factor, map.GetWidth());
				for (int sy = y * factor; sy < lastY; sy++) {
					for (int sx = x * factor; sx < lastX; sx++) {
						sum += map.At(sx, sy);
						count++;
					}
				}
				small.At(x, y) = static_cast<float>(sum / count);
			}
		}
		return small;
	}

	int FitDownsample(int width, int height)
	{
		int factor = 1;
		while ((width + factor - 1) / factor > maxMapSize || (height + factor - 1) / factor > maxMapSize) {
			factor *= 2;
		}
		return factor;
	}

	glm::dvec2 PixelSizeMetres(glm::dvec2 pixelSize, bool geographic, double latitude)
	{
		if (!geographic) {
			return pixelSize;
		}
		//Metres per degree of longitude shrink with the latitude; of latitude nearly constant (spherical approximation)
		constexpr double metresPerDegreeLongitude = 111320.0, metresPerDegreeLatitude = 110574.0;
		//A latitude beyond the poles comes from a broken file: kept short of 90 so the pixel stays positive
		const double radians = std::clamp(latitude, -89.9, 89.9) * 3.14159265358979323846 / 180.0;
		return glm::dvec2(pixelSize.x * metresPerDegreeLongitude * std::cos(radians), pixelSize.y * metresPerDegreeLatitude);
	}
}
