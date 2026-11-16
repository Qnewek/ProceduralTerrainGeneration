#include "Hydrology.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace hydrology {
	namespace {
		constexpr int neighbourCount = 8;
		constexpr int dx[neighbourCount] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		constexpr int dy[neighbourCount] = { 0, 1, 1, 1, 0, -1, -1, -1 };
		const float neighbourDistance[neighbourCount] = { 1.0f, std::sqrt(2.0f), 1.0f, std::sqrt(2.0f), 1.0f, std::sqrt(2.0f), 1.0f, std::sqrt(2.0f) };

		//Hash of a lattice point in [-1, 1]
		float Hash(int x, int y)
		{
			uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
			h = (h ^ (h >> 13)) * 1274126177u;
			h ^= h >> 16;
			return static_cast<float>(h & 0xffffffu) / static_cast<float>(0xffffff) * 2.0f - 1.0f;
		}

		//Smooth value noise in [-1, 1], one feature per unit
		float ValueNoise(glm::vec2 p)
		{
			const glm::vec2 cell = glm::floor(p);
			const glm::vec2 f = p - cell;
			const glm::vec2 s = f * f * (3.0f - 2.0f * f);
			const int x = static_cast<int>(cell.x), y = static_cast<int>(cell.y);
			const float bottom = glm::mix(Hash(x, y), Hash(x + 1, y), s.x);
			const float top = glm::mix(Hash(x, y + 1), Hash(x + 1, y + 1), s.x);
			return glm::mix(bottom, top, s.y);
		}

		//Wavelength of the meander noise in cells
		constexpr float meanderWavelength = 6.0f;

		struct Segment {
			glm::vec2 a, b;		//cell coordinates
			float levelA, levelB;
			float widthA, widthB;
		};

		//Writes the segment into every cell it is the nearest course of, within radius cells of it
		void Rasterize(const Segment& s, int radius, float cellSize, Grid<RiverTexel>& rivers, Grid<LevelTexel>& levels)
		{
			const int width = rivers.GetWidth(), height = rivers.GetHeight();
			const glm::vec2 lo = glm::min(s.a, s.b), hi = glm::max(s.a, s.b);
			const int x0 = std::max(0, static_cast<int>(std::floor(lo.x)) - radius), x1 = std::min(width - 1, static_cast<int>(std::ceil(hi.x)) + radius);
			const int y0 = std::max(0, static_cast<int>(std::floor(lo.y)) - radius), y1 = std::min(height - 1, static_cast<int>(std::ceil(hi.y)) + radius);
			const glm::vec2 ab = s.b - s.a;
			const float length2 = glm::dot(ab, ab);
			for (int y = y0; y <= y1; y++) {
				for (int x = x0; x <= x1; x++) {
					const glm::vec2 q(static_cast<float>(x), static_cast<float>(y));
					const float t = length2 > 0.0f ? std::clamp(glm::dot(q - s.a, ab) / length2, 0.0f, 1.0f) : 0.0f;
					const glm::vec2 offset = (s.a + ab * t - q) * cellSize;
					const float distance = glm::length(offset);
					RiverTexel& r = rivers.At(x, y);
					if (distance < r.distance) {
						r = { offset.x, offset.y, distance, glm::mix(s.widthA, s.widthB, t) };
						levels.At(x, y).river = glm::mix(s.levelA, s.levelB, t);
					}
				}
			}
		}

		//Number of 4-connected groups of lake cells
		int CountLakes(const std::vector<char>& lake, int width, int height)
		{
			std::vector<char> seen(lake.size(), 0);
			std::vector<int> stack;
			int count = 0;
			for (size_t start = 0; start < lake.size(); start++) {
				if (!lake[start] || seen[start]) {
					continue;
				}
				count++;
				seen[start] = 1;
				stack.push_back(static_cast<int>(start));
				while (!stack.empty()) {
					const int c = stack.back();
					stack.pop_back();
					const int x = c % width, y = c / width;
					const int next[4][2] = { { x + 1, y }, { x - 1, y }, { x, y + 1 }, { x, y - 1 } };
					for (const auto& n : next) {
						if (n[0] < 0 || n[1] < 0 || n[0] >= width || n[1] >= height) {
							continue;
						}
						const int i = n[1] * width + n[0];
						if (lake[i] && !seen[i]) {
							seen[i] = 1;
							stack.push_back(i);
						}
					}
				}
			}
			return count;
		}
	}

	Drainage Drain(const HeightField& terrain, float waterHeight)
	{
		const int width = terrain.GetWidth(), height = terrain.GetHeight();
		const size_t count = terrain.Size();
		Drainage d;
		d.filled.Resize(width, height);
		d.lakeLevel.Resize(width, height);
		d.receiver.assign(count, -1);
		d.order.reserve(count);
		d.area.assign(count, 1.0f);

		//Ordered by height, then by index, so the result does not depend on the queue implementation
		using Entry = std::pair<float, int>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
		std::vector<char> closed(count, 0);
		const auto seed = [&](int i) {
			if (!closed[i]) {
				closed[i] = 1;
				d.filled[i] = terrain[i];
				d.lakeLevel[i] = terrain[i];
				open.push({ terrain[i], i });
			}
		};
		for (int x = 0; x < width; x++) {
			seed(x);
			seed((height - 1) * width + x);
		}
		for (int y = 0; y < height; y++) {
			seed(y * width);
			seed(y * width + width - 1);
		}
		for (size_t i = 0; i < count; i++) {
			if (terrain[i] < waterHeight) {
				seed(static_cast<int>(i));
			}
		}

		//Priority flood + epsilon: a cell reached from a lower one is raised just above it, so every cell has a lower neighbour.
		//Cells leave the queue in non-decreasing filled height, so a receiver (strictly lower) always comes earlier in order
		while (!open.empty()) {
			const int c = open.top().second;
			open.pop();
			d.order.push_back(c);
			const int x = c % width, y = c / width;
			for (int k = 0; k < neighbourCount; k++) {
				const int nx = x + dx[k], ny = y + dy[k];
				if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
					continue;
				}
				const int n = ny * width + nx;
				if (closed[n]) {
					continue;
				}
				closed[n] = 1;
				d.filled[n] = std::max(terrain[n], std::nextafter(d.filled[c], std::numeric_limits<float>::infinity()));
				d.lakeLevel[n] = std::max(terrain[n], d.lakeLevel[c]);
				open.push({ d.filled[n], n });
			}
		}

		//Steepest descent on the filled surface; outlets (seeds) keep -1
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const int c = y * width + x;
				const bool border = x == 0 || y == 0 || x == width - 1 || y == height - 1;
				if (border || terrain[c] < waterHeight) {
					continue;
				}
				float steepest = 0.0f;
				for (int k = 0; k < neighbourCount; k++) {
					const int n = (y + dy[k]) * width + x + dx[k];
					const float slope = (d.filled[c] - d.filled[n]) / neighbourDistance[k];
					if (slope > steepest) {
						steepest = slope;
						d.receiver[c] = n;
					}
				}
			}
		}

		for (size_t i = d.order.size(); i-- > 0;) {
			const int c = d.order[i];
			if (d.receiver[c] >= 0) {
				d.area[d.receiver[c]] += d.area[c];
			}
		}
		return d;
	}

	Network Compute(const HeightField& terrain, glm::vec2 origin, float cellSize, float waterHeight, const rivers::RiverConfig& config)
	{
		const int width = terrain.GetWidth(), height = terrain.GetHeight();
		const size_t count = terrain.Size();
		Network net;
		net.origin = origin;
		net.cellSize = cellSize;
		net.rivers.Resize(width, height);
		net.levels.Resize(width, height);
		if (count == 0) {
			return net;
		}
		const Drainage d = Drain(terrain, waterHeight);

		std::vector<char> lake(count, 0), river(count, 0);
		const float cellArea = cellSize * cellSize;
		for (size_t c = 0; c < count; c++) {
			lake[c] = d.lakeLevel[c] >= waterHeight && d.lakeLevel[c] - terrain[c] > config.minLakeDepth;
			river[c] = d.receiver[c] >= 0 && !lake[c] && terrain[c] >= waterHeight && d.area[c] * cellArea >= config.sourceArea;
			if (lake[c]) {
				net.levels[c].lake = d.lakeLevel[c];
				net.lakeCells++;
			}
		}
		net.lakes = CountLakes(lake, width, height);

		//Surface where a river ends (a lake, the sea or the border of the region)
		const auto endLevel = [&](int c) {
			return lake[c] ? d.lakeLevel[c] : std::max(d.lakeLevel[c] - config.surfaceDrop, waterHeight);
		};
		//Receivers come first, so a river never rises downstream
		std::vector<float> level(count, 0.0f), halfWidth(count, 0.0f);
		for (const int c : d.order) {
			if (!river[c]) {
				continue;
			}
			const int r = d.receiver[c];
			level[c] = std::max(std::max(d.lakeLevel[c] - config.surfaceDrop, waterHeight), river[r] ? level[r] : endLevel(r));
			halfWidth[c] = std::clamp(config.minHalfWidth * std::sqrt(d.area[c] * cellArea / config.sourceArea), config.minHalfWidth,
				std::max(config.minHalfWidth, config.maxHalfWidth));
			net.riverCells++;
		}

		//Main donor of every river cell: the river draining the largest area into it; sources have none
		std::vector<int> up(count, -1);
		for (size_t c = 0; c < count; c++) {
			const int r = d.receiver[c];
			if (river[c] && river[r] && (up[r] < 0 || d.area[c] > d.area[up[r]])) {
				up[r] = static_cast<int>(c);
			}
		}
		const auto center = [&](int c) { return glm::vec2(static_cast<float>(c % width), static_cast<float>(c / width)); };
		std::vector<glm::vec2> point(count, glm::vec2(0.0f));
		for (size_t c = 0; c < count; c++) {
			point[c] = center(static_cast<int>(c));
		}

		//Bends across the course, then smoothing along it; sources and the ends (lake, sea) stay in place
		const float amplitude = config.meander / cellSize;
		if (amplitude > 0.0f) {
			std::vector<glm::vec2> bent = point;
			for (size_t c = 0; c < count; c++) {
				if (!river[c] || up[c] < 0) {
					continue;
				}
				const glm::vec2 along = point[d.receiver[c]] - point[up[c]];
				if (glm::dot(along, along) <= 0.0f) {
					continue;
				}
				const glm::vec2 across = glm::normalize(glm::vec2(-along.y, along.x));
				bent[c] = point[c] + across * amplitude * ValueNoise(point[c] / meanderWavelength);
			}
			point = std::move(bent);
		}
		for (int pass = 0; pass < config.smoothing; pass++) {
			std::vector<glm::vec2> smoothed = point;
			for (size_t c = 0; c < count; c++) {
				if (river[c] && up[c] >= 0) {
					smoothed[c] = 0.25f * point[up[c]] + 0.5f * point[c] + 0.25f * point[d.receiver[c]];
				}
			}
			point = std::move(smoothed);
		}

		//The carving reaches maxHalfWidth + bankWidth texels from a course; one more cell keeps bilinear sampling inside written cells
		const int radius = static_cast<int>(std::ceil((std::max(config.minHalfWidth, config.maxHalfWidth) + config.bankWidth) / cellSize)) + 1;
		for (size_t c = 0; c < count; c++) {
			if (!river[c]) {
				continue;
			}
			const int r = d.receiver[c];
			const Segment s{ point[c], river[r] ? point[r] : center(r), level[c], river[r] ? level[r] : endLevel(r), halfWidth[c],
				river[r] ? halfWidth[r] : halfWidth[c] };
			Rasterize(s, radius, cellSize, net.rivers, net.levels);
		}
		return net;
	}
}
