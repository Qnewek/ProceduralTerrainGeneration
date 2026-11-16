#include "doctest/doctest.h"

#include <cmath>
#include <functional>

#include "Hydrology.h"

namespace {
	//Not square, so swapped axes are detected
	constexpr int width = 61, height = 43;

	HeightField MakeMap(const std::function<float(int, int)>& h)
	{
		HeightField map(width, height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				map.At(x, y) = h(x, y);
			}
		}
		return map;
	}

	//Valley along x = 30 falling towards y = 0, sides rising away from it
	float Valley(int x, int y)
	{
		return 0.2f + 0.004f * static_cast<float>(y) + 0.01f * std::abs(static_cast<float>(x - 30));
	}
}

TEST_CASE("Drainage: water drains from every cell and the area adds up")
{
	const HeightField map = MakeMap([](int x, int y) { return 0.1f + 0.01f * static_cast<float>(x) + 0.001f * static_cast<float>(y); });
	const hydrology::Drainage d = hydrology::Drain(map, 0.0f);
	CHECK(d.order.size() == map.Size());
	std::vector<int> position(map.Size());
	for (size_t i = 0; i < d.order.size(); i++) {
		position[d.order[i]] = static_cast<int>(i);
	}
	float outletArea = 0.0f;
	for (size_t c = 0; c < map.Size(); c++) {
		const int r = d.receiver[c];
		if (r < 0) {
			outletArea += d.area[c];
			continue;
		}
		CHECK(d.filled[r] < d.filled[c]);
		CHECK(position[r] < position[c]);
	}
	//Everything ends at an outlet exactly once
	CHECK(outletArea == doctest::Approx(static_cast<float>(map.Size())));
}

TEST_CASE("Drainage: a depression fills into a lake drained over its rim")
{
	//Plane falling towards x = 0 with a pit of depth 0.1 around (40, 20)
	const HeightField map = MakeMap([](int x, int y) {
		const float pit = std::hypot(static_cast<float>(x - 40), static_cast<float>(y - 20));
		return 0.3f + 0.002f * static_cast<float>(x) - (pit < 6.0f ? 0.1f * (1.0f - pit / 6.0f) : 0.0f);
	});
	const hydrology::Drainage d = hydrology::Drain(map, 0.0f);
	const size_t pit = 20 * width + 40;
	CHECK(d.lakeLevel[pit] > map[pit] + 0.05f);
	for (size_t c = 0; c < map.Size(); c++) {
		CHECK(d.filled[c] >= map[c]);
		CHECK(d.lakeLevel[c] >= map[c]);
		CHECK(d.filled[c] >= d.lakeLevel[c]);
	}
	//The lake has no outlet of its own: the water leaves it towards the border
	int c = static_cast<int>(pit);
	int steps = 0;
	while (d.receiver[c] >= 0 && steps < width * height) {
		c = d.receiver[c];
		steps++;
	}
	CHECK(c % width == 0);
}

TEST_CASE("Rivers: a river follows the valley and never rises downstream")
{
	rivers::RiverConfig config;
	config.sourceArea = 50.0f;
	config.meander = 0.0f;
	config.smoothing = 0;
	config.surfaceDrop = 0.0f;
	const HeightField map = MakeMap(Valley);
	const hydrology::Network net = hydrology::Compute(map, glm::vec2(0.0f), 1.0f, 0.0f, config);
	CHECK(net.riverCells > 0);
	CHECK(net.lakes == 0);
	float previous = 1.0f;
	for (int y = height - 3; y > 0; y--) {
		const hydrology::RiverTexel& r = net.rivers.At(30, y);
		CHECK(r.distance < 0.5f);
		const float level = net.levels.At(30, y).river;
		CHECK(level <= previous);
		CHECK(level <= map.At(30, y) + 1e-6f);
		previous = level;
		//The offset points at the course
		const hydrology::RiverTexel& side = net.rivers.At(33, y);
		CHECK(side.offsetX == doctest::Approx(-3.0f));
		CHECK(side.distance == doctest::Approx(3.0f));
	}
	//Wider downstream
	CHECK(net.rivers.At(30, 2).halfWidth >= net.rivers.At(30, height - 10).halfWidth);
}

TEST_CASE("Rivers: nothing far from the courses, under the sea or in a flat lake")
{
	rivers::RiverConfig config;
	config.sourceArea = 50.0f;
	config.bankWidth = 4.0f;
	config.maxHalfWidth = 4.0f;
	SUBCASE("far from the valley")
	{
		const hydrology::Network net = hydrology::Compute(MakeMap(Valley), glm::vec2(0.0f), 1.0f, 0.0f, config);
		CHECK(net.rivers.At(2, height / 2).distance == hydrology::farDistance);
	}
	SUBCASE("under the sea")
	{
		const hydrology::Network net = hydrology::Compute(MakeMap([](int, int) { return 0.1f; }), glm::vec2(0.0f), 1.0f, 0.5f, config);
		CHECK(net.riverCells == 0);
		CHECK(net.lakeCells == 0);
	}
	SUBCASE("lake")
	{
		const HeightField bowl = MakeMap([](int x, int y) {
			return 0.2f + 0.0004f * static_cast<float>((x - 30) * (x - 30) + (y - 21) * (y - 21)) + (x == 0 || y == 0 || x == width - 1 || y == height - 1 ? 1.0f : 0.0f);
		});
		const hydrology::Network net = hydrology::Compute(bowl, glm::vec2(0.0f), 1.0f, 0.0f, config);
		CHECK(net.lakes == 1);
		CHECK(net.levels.At(30, 21).lake > bowl.At(30, 21));
		CHECK(net.levels.At(30, 21).lake == doctest::Approx(net.levels.At(31, 21).lake));
	}
}
