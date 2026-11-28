#include "Hydrology.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

#include "DemTools.h"

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
			float foamA, foamB;
			//Waterfall attached to the segment (levels above are then continuous across it, see LevelTexel); fallDrop 0 = none
			float fallDrop = 0.0f;
			glm::vec2 fallPoint = glm::vec2(0.0f), fallDirection = glm::vec2(0.0f, 1.0f);	//cell coordinates, downstream
		};

		//Writes the segment into every cell it is the nearest course of, within radius cells of it
		void Rasterize(const Segment& s, int radius, float cellSize, Network& net)
		{
			Grid<RiverTexel>& rivers = net.rivers;
			const int width = rivers.GetWidth(), height = rivers.GetHeight();
			const glm::vec2 lo = glm::min(s.a, s.b), hi = glm::max(s.a, s.b);
			const int x0 = std::max(0, static_cast<int>(std::floor(lo.x)) - radius), x1 = std::min(width - 1, static_cast<int>(std::ceil(hi.x)) + radius);
			const int y0 = std::max(0, static_cast<int>(std::floor(lo.y)) - radius), y1 = std::min(height - 1, static_cast<int>(std::ceil(hi.y)) + radius);
			const glm::vec2 ab = s.b - s.a;
			const float length2 = glm::dot(ab, ab);
			const float length = std::sqrt(length2);
			FlowTexel flow = length > 0.0f
				? FlowTexel{ ab.x / length, ab.y / length, std::max(s.levelA - s.levelB, 0.0f) / (length * cellSize), 0.0f } : FlowTexel{};
			for (int y = y0; y <= y1; y++) {
				for (int x = x0; x <= x1; x++) {
					const glm::vec2 q(static_cast<float>(x), static_cast<float>(y));
					const float t = length2 > 0.0f ? std::clamp(glm::dot(q - s.a, ab) / length2, 0.0f, 1.0f) : 0.0f;
					const glm::vec2 offset = (s.a + ab * t - q) * cellSize;
					const float distance = glm::length(offset);
					RiverTexel& r = rivers.At(x, y);
					if (distance < r.distance) {
						r = { offset.x, offset.y, distance, glm::mix(s.widthA, s.widthB, t) };
						LevelTexel& level = net.levels.At(x, y);
						level.river = glm::mix(s.levelA, s.levelB, t);
						level.fallDrop = s.fallDrop;
						level.fallAlong = s.fallDrop > 0.0f ? s.fallDrop * glm::dot(s.fallPoint - q, s.fallDirection) * cellSize : 0.0f;
						flow.foam = glm::mix(s.foamA, s.foamB, t);
						net.flow.At(x, y) = flow;
					}
				}
			}
		}

		//Clears the 4-connected groups of lake cells with fewer than minCells cells
		//@return number of groups kept
		int RemoveSmallLakes(std::vector<char>& lake, int width, int height, int minCells)
		{
			std::vector<char> seen(lake.size(), 0);
			std::vector<int> stack, group;
			int count = 0;
			for (size_t start = 0; start < lake.size(); start++) {
				if (!lake[start] || seen[start]) {
					continue;
				}
				group.clear();
				seen[start] = 1;
				stack.push_back(static_cast<int>(start));
				while (!stack.empty()) {
					const int c = stack.back();
					stack.pop_back();
					group.push_back(c);
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
				if (static_cast<int>(group.size()) < minCells) {
					for (const int c : group) {
						lake[c] = 0;
					}
				}
				else {
					count++;
				}
			}
			return count;
		}
	}

	float FallStep(float along)
	{
		const float t = std::clamp((along + fallEdge) / (2.0f * fallEdge), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	float RiverLevel(float river, float fallDrop, float fallAlong)
	{
		return fallDrop > 0.0f ? river + fallDrop * FallStep(fallAlong / fallDrop) : river;
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
		if (count == 0) {
			return d;
		}
		auto finiteHeight = [&](size_t i) { return std::isfinite(terrain[i]) ? terrain[i] : 0.0f; };

		//Ordered by height, then by index, so the result does not depend on the queue implementation
		using Entry = std::pair<float, int>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
		std::vector<char> closed(count, 0);
		const auto seed = [&](int i) {
			if (!closed[i]) {
				closed[i] = 1;
				const float h = finiteHeight(static_cast<size_t>(i));
				d.filled[i] = h;
				d.lakeLevel[i] = h;
				open.push({ h, i });
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
			if (finiteHeight(i) < waterHeight) {
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
				const float h = finiteHeight(static_cast<size_t>(n));
				d.filled[n] = std::max(h, std::nextafter(d.filled[c], std::numeric_limits<float>::infinity()));
				d.lakeLevel[n] = std::max(h, d.lakeLevel[c]);
				open.push({ d.filled[n], n });
			}
		}

		//Steepest descent on the filled surface; outlets (seeds) keep -1
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const int c = y * width + x;
				const bool border = x == 0 || y == 0 || x == width - 1 || y == height - 1;
				if (border || finiteHeight(static_cast<size_t>(c)) < waterHeight) {
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
		net.flow.Resize(width, height);
		net.ground = terrain;
		if (count == 0 || !(cellSize > 0.0f) || !std::isfinite(cellSize) || !std::isfinite(waterHeight)
			|| !(config.sourceArea > 0.0f) || !std::isfinite(config.sourceArea)) {
			return net;
		}
		const Drainage d = Drain(terrain, waterHeight);

		std::vector<char> lake(count, 0), river(count, 0);
		const float cellArea = cellSize * cellSize;
		auto finiteHeight = [&](size_t i) { return std::isfinite(terrain[i]) ? terrain[i] : 0.0f; };
		for (size_t c = 0; c < count; c++) {
			lake[c] = d.lakeLevel[c] >= waterHeight && d.lakeLevel[c] - finiteHeight(c) > config.minLakeDepth;
		}
		//Small lakes are land: a river may cross them (its surface at the spill level, as it never rises downstream)
		const double minCells = std::ceil(static_cast<double>(config.minLakeArea) / static_cast<double>(cellArea));
		net.lakes = RemoveSmallLakes(lake, width, height, static_cast<int>(std::min(minCells, static_cast<double>(count) + 1.0)));
		for (size_t c = 0; c < count; c++) {
			river[c] = d.receiver[c] >= 0 && !lake[c] && finiteHeight(c) >= waterHeight && d.area[c] * cellArea >= config.sourceArea;
			if (lake[c]) {
				net.levels[c].lake = d.lakeLevel[c];
				net.lakeCells++;
			}
		}

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

		const auto end = [&](int c) { return river[d.receiver[c]] ? point[d.receiver[c]] : center(d.receiver[c]); };
		const auto endLevelOf = [&](int c) { return river[d.receiver[c]] ? level[d.receiver[c]] : endLevel(d.receiver[c]); };
		const auto segmentSlope = [&](int c) {
			const float length = glm::length(end(c) - point[c]) * cellSize;
			return length > 0.0f ? std::max(level[c] - endLevelOf(c), 0.0f) / length : 0.0f;
		};

		//Waterfalls: runs of steep segments (gaps of up to 2 gentle ones joined) keep the level of their head, the rest lies at the level of the
		//bottom of the run; the fall at the head is attached to the segments within 2 of it (see LevelTexel)
		std::vector<int> fallAt(count, -1);			//fall attached to the segment from the cell
		std::vector<char> startAbove(count, 0);		//the start of the segment lies upstream of the fall line (the head counts as upstream)
		std::vector<char> endAbove(count, 0);		//its end too
		std::vector<float> fallFoam(count, 0.0f);
		if (config.waterfalls) {
			constexpr int far = 1 << 20;
			std::vector<char> steep(count, 0);
			for (size_t c = 0; c < count; c++) {
				steep[c] = river[c] && segmentSlope(static_cast<int>(c)) > config.fallSlope;
			}
			//Segments to the nearest steep one downstream (1 = the next) and upstream (1 = a donor's)
			std::vector<int> down(count, far), upward(count, far);
			for (const int c : d.order) {
				const int r = d.receiver[c];
				if (river[c] && river[r]) {
					down[c] = steep[r] ? 1 : std::min(down[r] + 1, far);
				}
			}
			for (auto it = d.order.rbegin(); it != d.order.rend(); ++it) {
				const int c = *it;
				const int r = d.receiver[c];
				if (river[c] && river[r]) {
					upward[r] = std::min(upward[r], steep[c] ? 1 : std::min(upward[c] + 1, far));
				}
			}
			std::vector<char> run(count, 0);
			for (size_t c = 0; c < count; c++) {
				run[c] = steep[c] || (river[c] && upward[c] + down[c] <= 3);
			}
			//Bottom of the run of every cell in it (receivers first)
			std::vector<float> bottom(count, 0.0f);
			for (const int c : d.order) {
				if (run[c]) {
					const int r = d.receiver[c];
					bottom[c] = river[r] && run[r] ? bottom[r] : endLevelOf(c);
				}
			}
			std::vector<std::vector<int>> donors(count);
			for (size_t c = 0; c < count; c++) {
				if (river[c] && river[d.receiver[c]]) {
					donors[static_cast<size_t>(d.receiver[c])].push_back(static_cast<int>(c));
				}
			}
			const auto isHead = [&](size_t c) { return run[c] && (up[c] < 0 || !run[static_cast<size_t>(up[c])]); };
			std::vector<int> heads;
			for (size_t c = 0; c < count; c++) {
				if (isHead(c) && level[c] > bottom[c]) {
					heads.push_back(static_cast<int>(c));
				}
			}
			std::vector<float> drops(heads.size());
			for (size_t i = 0; i < heads.size(); i++) {
				drops[i] = level[heads[i]] - bottom[heads[i]];
			}
			for (size_t c = 0; c < count; c++) {
				if (run[c] && !isHead(c)) {
					level[c] = bottom[c];
				}
			}
			//A segment two falls would share takes the larger drop
			std::vector<float> attachedDrop(count, 0.0f);
			const auto attach = [&](int c, int fall, bool start, bool endToo) {
				const float drop = net.falls[static_cast<size_t>(fall)].drop;
				if (drop > attachedDrop[c]) {
					attachedDrop[c] = drop;
					fallAt[c] = fall;
					startAbove[c] = start ? 1 : 0;
					endAbove[c] = endToo ? 1 : 0;
				}
			};
			for (size_t i = 0; i < heads.size(); i++) {
				const int h = heads[i];
				const glm::vec2 along = end(h) - point[h];
				const glm::vec2 direction = glm::dot(along, along) > 0.0f ? glm::normalize(along) : glm::vec2(0.0f, 1.0f);
				const int fall = static_cast<int>(net.falls.size());
				net.falls.push_back({ origin + point[h] * cellSize, direction, drops[i] });
				fallFoam[h] = drops[i] / cellSize;
				attach(h, fall, true, false);
				const int r = d.receiver[h];
				if (river[r]) {
					attach(r, fall, false, false);
				}
				for (const int u : donors[h]) {
					attach(u, fall, true, true);
					for (const int u2 : donors[u]) {
						attach(u2, fall, true, true);
					}
				}
			}
		}

		//Foam below falls: every point of a course gets the steepest segment slope (or waterfall) upstream of it, fading with the distance from it
		//(donors come after their receivers in the order, so going backwards every cell is final before its receiver takes it)
		std::vector<float> foam = fallFoam;
		for (auto it = d.order.rbegin(); it != d.order.rend(); ++it) {
			const int c = *it;
			if (!river[c]) {
				continue;
			}
			const float length = glm::length(end(c) - point[c]) * cellSize;
			const int r = d.receiver[c];
			foam[r] = std::max(foam[r], std::max(segmentSlope(c), foam[c] * std::exp(-length / foamLength)));
		}

		//The carving reaches maxHalfWidth + bankWidth texels from a course; one more cell keeps bilinear sampling inside written cells
		const int radius = static_cast<int>(std::ceil((std::max(config.minHalfWidth, config.maxHalfWidth) + config.bankWidth) / cellSize)) + 1;
		for (size_t c = 0; c < count; c++) {
			if (!river[c]) {
				continue;
			}
			const int r = d.receiver[c];
			Segment s{ point[c], end(static_cast<int>(c)), level[c], endLevelOf(static_cast<int>(c)), halfWidth[c],
				river[r] ? halfWidth[r] : halfWidth[c], foam[c], foam[r] };
			//Near a fall the level without its drop: continuous across the fall line, the drop added back upstream of it
			if (fallAt[c] >= 0) {
				const Fall& fall = net.falls[static_cast<size_t>(fallAt[c])];
				s.fallDrop = fall.drop;
				s.fallPoint = (fall.position - origin) / cellSize;
				s.fallDirection = fall.direction;
				s.levelA -= startAbove[c] ? fall.drop : 0.0f;
				s.levelB -= endAbove[c] ? fall.drop : 0.0f;
			}
			Rasterize(s, radius, cellSize, net);
		}
		return net;
	}

	float WaterLevelAt(const Network& network, glm::vec2 position)
	{
		constexpr float none = -std::numeric_limits<float>::infinity();
		const int width = network.rivers.GetWidth(), height = network.rivers.GetHeight();
		const glm::vec2 cell = (position - network.origin) / network.cellSize;
		if (width < 2 || height < 2 || cell.x < 0.0f || cell.y < 0.0f || cell.x > static_cast<float>(width - 1) || cell.y > static_cast<float>(height - 1)) {
			return none;
		}
		const int x = std::min(static_cast<int>(cell.x), width - 2), y = std::min(static_cast<int>(cell.y), height - 2);
		const glm::vec2 f = cell - glm::vec2(static_cast<float>(x), static_cast<float>(y));
		const auto bilinear = [&](auto value) {
			const float bottom = glm::mix(value(x, y), value(x + 1, y), f.x);
			const float top = glm::mix(value(x, y + 1), value(x + 1, y + 1), f.x);
			return glm::mix(bottom, top, f.y);
		};
		const glm::vec2 offset(bilinear([&](int i, int j) { return network.rivers.At(i, j).offsetX; }),
			bilinear([&](int i, int j) { return network.rivers.At(i, j).offsetY; }));
		const float distance = std::max(glm::length(offset), bilinear([&](int i, int j) { return network.rivers.At(i, j).distance; }) - network.cellSize);
		const float halfWidth = bilinear([&](int i, int j) { return network.rivers.At(i, j).halfWidth; });
		float level = distance < std::max(halfWidth, 0.5f) ? RiverLevel(bilinear([&](int i, int j) { return network.levels.At(i, j).river; }),
			bilinear([&](int i, int j) { return network.levels.At(i, j).fallDrop; }), bilinear([&](int i, int j) { return network.levels.At(i, j).fallAlong; }))
			: none;
		//Lake of the nearest cell
		const LevelTexel& nearest = network.levels.At(static_cast<int>(cell.x + 0.5f), static_cast<int>(cell.y + 0.5f));
		if (nearest.lake != noLake) {
			level = std::max(level, nearest.lake);
		}
		return level;
	}

	float FlowSpeed(float slope)
	{
		//About 0.5 m/s on a plain, a few m/s on a mountain stream
		return std::clamp(0.4f + 6.0f * std::sqrt(std::max(slope, 0.0f)), 0.4f, 4.0f);
	}

	std::vector<WaterVertex> BuildWaterMesh(const Network& network, float waterHeight, float texelsPerHeight)
	{
		const int width = network.rivers.GetWidth(), height = network.rivers.GetHeight();
		const float reach = 1.5f * network.cellSize;
		//The surface sinks this far (1 texel) under the ground where it must not show; the ground between cells is interpolated
		const float sink = 1.0f / std::max(texelsPerHeight, 1.0f);
		const bool hasGround = network.ground.Size() == network.rivers.Size();
		//Below the ground at a cell: carving never lowers the terrain outside a channel under min(ground, river level)
		const auto underGround = [&](int c, float l) {
			return hasGround ? std::min(l, network.ground[c] - sink) : l;
		};
		//Surface of every cell, waterHeight where it is dry (or not above the sea)
		std::vector<float> level(network.rivers.Size(), waterHeight);
		std::vector<char> wet(network.rivers.Size(), 0);
		//Cells whose surface is the river in its channel with a waterfall near: quads touching them are cut to texels, so the fall is a step
		std::vector<char> falling(network.rivers.Size(), 0);
		for (size_t c = 0; c < level.size(); c++) {
			const RiverTexel& r = network.rivers[c];
			const LevelTexel& lt = network.levels[c];
			float l = lt.lake;
			if (r.distance < r.halfWidth + reach) {
				const float river = RiverLevel(lt.river, lt.fallDrop, lt.fallAlong);
				const bool channel = r.distance < r.halfWidth;
				falling[c] = channel && lt.fallDrop > 0.0f && river >= l;
				l = std::max(l, channel ? river : underGround(static_cast<int>(c), river));
			}
			if (l > waterHeight) {
				level[c] = l;
				wet[c] = 1;
			}
			falling[c] = falling[c] && wet[c];
		}
		//The waterfall term of a cell (0 where it is not falling)
		const auto fallTerm = [&](int c) {
			const LevelTexel& lt = network.levels[c];
			return falling[c] ? lt.fallDrop * FallStep(lt.fallAlong / lt.fallDrop) : 0.0f;
		};
		const int subdivisions = std::clamp(static_cast<int>(std::lround(network.cellSize)), 1, 16);

		std::vector<WaterVertex> vertices;
		for (int y = 0; y + 1 < height; y++) {
			for (int x = 0; x + 1 < width; x++) {
				const int corners[4] = { y * width + x, y * width + x + 1, (y + 1) * width + x, (y + 1) * width + x + 1 };
				float top = waterHeight;
				bool any = false;
				for (const int c : corners) {
					if (wet[c]) {
						any = true;
						top = std::max(top, level[c]);
					}
				}
				if (!any) {
					continue;
				}
				WaterVertex v[4];
				for (int k = 0; k < 4; k++) {
					const int c = corners[k];
					const glm::vec2 p = network.origin + glm::vec2(static_cast<float>(c % width), static_cast<float>(c / width)) * network.cellSize;
					v[k] = { p.x, p.y, wet[c] ? level[c] : underGround(c, top), 0.0f, 0.0f, 0.0f, 0.0f };
					//Lakes stand still; a river flows along its course
					const FlowTexel& f = network.flow[c];
					if (wet[c] && network.levels[c].lake == noLake) {
						const float speed = FlowSpeed(f.slope * texelsPerHeight);
						v[k].flowX = f.x * speed;
						v[k].flowY = f.y * speed;
						v[k].slope = f.slope;
					}
					//Foam on the river and on a lake where a fall enters it, fading out beside the course
					if (wet[c]) {
						const RiverTexel& r = network.rivers[c];
						const float beside = std::clamp((r.distance - r.halfWidth) / reach, 0.0f, 1.0f);
						v[k].foam = f.foam * (1.0f - beside);
					}
				}
				const bool fall = falling[corners[0]] || falling[corners[1]] || falling[corners[2]] || falling[corners[3]];
				if (!fall) {
					for (const int k : { 0, 1, 2, 2, 1, 3 }) {
						vertices.push_back(v[k]);
					}
					continue;
				}
				//At a waterfall: texel quads, the corners interpolated with the fall evaluated exactly (the fields of LevelTexel interpolated, as
				//the GPU samples them). An edge shared with a plain quad has no falling corner, so its fall term is 0 and the edge matches
				float drop[4], along[4], term[4];
				for (int k = 0; k < 4; k++) {
					drop[k] = falling[corners[k]] ? network.levels[corners[k]].fallDrop : 0.0f;
					along[k] = falling[corners[k]] ? network.levels[corners[k]].fallAlong : 0.0f;
					term[k] = fallTerm(corners[k]);
				}
				std::vector<WaterVertex> grid(static_cast<size_t>(subdivisions + 1) * (subdivisions + 1));
				for (int j = 0; j <= subdivisions; j++) {
					for (int i = 0; i <= subdivisions; i++) {
						const float fx = static_cast<float>(i) / subdivisions, fy = static_cast<float>(j) / subdivisions;
						const float w[4] = { (1.0f - fx) * (1.0f - fy), fx * (1.0f - fy), (1.0f - fx) * fy, fx * fy };
						const auto mixed = [&](auto value) { return w[0] * value(0) + w[1] * value(1) + w[2] * value(2) + w[3] * value(3); };
						WaterVertex g;
						g.x = glm::mix(v[0].x, v[1].x, fx);
						g.y = glm::mix(v[0].y, v[2].y, fy);
						g.flowX = mixed([&](int k) { return v[k].flowX; });
						g.flowY = mixed([&](int k) { return v[k].flowY; });
						g.slope = mixed([&](int k) { return v[k].slope; });
						g.foam = mixed([&](int k) { return v[k].foam; });
						g.level = mixed([&](int k) { return v[k].level; }) - mixed([&](int k) { return term[k]; });
						const float d = mixed([&](int k) { return drop[k]; });
						if (d > 0.0f) {
							const float a = mixed([&](int k) { return along[k]; }) / d;
							g.level += d * FallStep(a);
							//The curtain: the drop over the texel around the vertex
							g.slope += d * (FallStep(a + 0.5f) - FallStep(a - 0.5f));
						}
						grid[static_cast<size_t>(j) * (subdivisions + 1) + i] = g;
					}
				}
				for (int j = 0; j < subdivisions; j++) {
					for (int i = 0; i < subdivisions; i++) {
						const size_t a = static_cast<size_t>(j) * (subdivisions + 1) + i;
						const size_t b = a + 1, e = a + subdivisions + 1, f = e + 1;
						for (const size_t k : { a, b, e, e, b, f }) {
							vertices.push_back(grid[k]);
						}
					}
				}
			}
		}
		return vertices;
	}

	MapRivers ComputeOnMap(const HeightField& metres, double pixelMetres, std::optional<float> seaMetres, const rivers::RiverConfig& config)
	{
		MapRivers result;
		const int cell = std::max(config.cellSize, 1);
		const int cellsX = metres.GetWidth() / cell, cellsY = metres.GetHeight() / cell;
		if (cellsX < 1 || cellsY < 1) {
			result.error = "The map is smaller than one river cell";
			return result;
		}
		//Whole cells only, so every cell centre lies (cell - 1) / 2 texels from the corner of its block
		HeightField sample = dem::Downsample(dem::Crop(metres, glm::ivec2(0), glm::ivec2(cellsX, cellsY) * cell), cell);
		for (size_t i = 0; i < sample.Size(); i++) {
			sample[i] /= mapMetresPerHeight;
		}
		const float waterHeight = seaMetres ? *seaMetres / mapMetresPerHeight
			: *std::min_element(sample.Data(), sample.Data() + sample.Size()) - 1.0f;
		const glm::vec2 origin(0.5f * static_cast<float>(cell - 1));
		//No waterfalls: the map is not carved, so water lowered below a fall would lie under the terrain
		rivers::RiverConfig mapConfig = config;
		mapConfig.waterfalls = false;
		result.network = Compute(sample, origin, static_cast<float>(cell), waterHeight, mapConfig);
		result.water = BuildWaterMesh(result.network, waterHeight, static_cast<float>(mapMetresPerHeight / pixelMetres));
		return result;
	}

	float RegionStep(int regionSize, int cellSize)
	{
		const int cell = std::max(cellSize, 1);
		return static_cast<float>(std::max(regionSize / 4 / cell, 1) * cell);
	}

	glm::vec2 SnapRegionCenter(glm::vec2 position, int regionSize, int cellSize)
	{
		const float step = RegionStep(regionSize, cellSize);
		return glm::vec2(std::round(position.x / step), std::round(position.y / step)) * step;
	}

	bool LeftRegion(const RegionPlace& region, glm::vec2 position)
	{
		const glm::vec2 distance = glm::abs(position - region.center);
		return distance.x > region.step || distance.y > region.step;
	}

	RegionPlace PlaceRegion(bool infiniteWorld, bool followCamera, const std::optional<RegionPlace>& current, glm::vec2 cameraPosition, glm::vec2 mapCenter,
		int regionSize, int cellSize)
	{
		RegionPlace place;
		place.step = RegionStep(regionSize, cellSize);
		if (!infiniteWorld) {
			place.center = mapCenter;
		}
		else if (followCamera) {
			place.center = SnapRegionCenter(cameraPosition, regionSize, cellSize);
		}
		else {
			place.center = current ? current->center : mapCenter;
		}
		return place;
	}
}
