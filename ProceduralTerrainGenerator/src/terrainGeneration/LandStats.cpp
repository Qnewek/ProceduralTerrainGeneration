#include "LandStats.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace land {
	LandStats Measure(const HeightField& map, float waterHeight)
	{
		LandStats stats;
		const int width = map.GetWidth(), height = map.GetHeight();
		if (map.Empty()) {
			return stats;
		}
		const size_t total = map.Size();

		//Flood fill of every landmass; a texel is visited once
		std::vector<uint8_t> visited(total, 0);
		std::vector<size_t> stack;
		size_t water = 0, land = 0, largest = 0, continental = 0;
		const size_t continentMin = static_cast<size_t>(continentMinShare * static_cast<float>(total));
		for (size_t start = 0; start < total; start++) {
			if (map[start] < waterHeight) {
				water++;
				continue;
			}
			if (visited[start]) {
				continue;
			}
			size_t size = 0;
			visited[start] = 1;
			stack.push_back(start);
			while (!stack.empty()) {
				const size_t i = stack.back();
				stack.pop_back();
				size++;
				const int x = static_cast<int>(i % width), y = static_cast<int>(i / width);
				const auto visit = [&](int nx, int ny) {
					const size_t n = static_cast<size_t>(ny) * width + nx;
					if (!visited[n] && map[n] >= waterHeight) {
						visited[n] = 1;
						stack.push_back(n);
					}
				};
				if (x > 0) visit(x - 1, y);
				if (x + 1 < width) visit(x + 1, y);
				if (y > 0) visit(x, y - 1);
				if (y + 1 < height) visit(x, y + 1);
			}
			stats.landmasses++;
			land += size;
			largest = std::max(largest, size);
			if (size >= continentMin) {
				continental += size;
			}
		}

		stats.waterShare = static_cast<float>(water) / static_cast<float>(total);
		if (land > 0) {
			stats.largestShare = static_cast<float>(largest) / static_cast<float>(land);
			stats.continentalShare = static_cast<float>(continental) / static_cast<float>(land);
		}
		return stats;
	}
}
