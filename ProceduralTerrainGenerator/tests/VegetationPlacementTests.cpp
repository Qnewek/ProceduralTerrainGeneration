#include "doctest/doctest.h"

#include <algorithm>
#include <functional>
#include <set>
#include <vector>

#include "Biome.h"
#include "VegetationPlacement.h"

using namespace vegetation;

namespace {
	constexpr int chunkTexels = 128;
	constexpr int mapSize = chunkTexels + 3;

	//Maps of the chunk starting at chunkTexel, with one texel of apron as ChunkWorld has them
	struct TestMaps {
		std::vector<float> height;
		std::vector<glm::vec4> surface;
		std::vector<int> ids;
		ChunkMaps maps;

		TestMaps(glm::ivec2 chunkTexel, const std::function<float(glm::vec2)>& heightAt, glm::vec2 gradient, int id)
		{
			height.resize(mapSize * mapSize);
			surface.resize(mapSize * mapSize, glm::vec4(gradient, 0.0f, 0.0f));
			ids.resize(mapSize * mapSize, id);
			maps.size = mapSize;
			maps.firstTexel = chunkTexel - 1;
			for (int y = 0; y < mapSize; y++) {
				for (int x = 0; x < mapSize; x++) {
					height[y * mapSize + x] = heightAt(glm::vec2(maps.firstTexel + glm::ivec2(x, y)));
				}
			}
			maps.height = height.data();
			maps.surface = surface.data();
			maps.ids = ids.data();
		}
	};

	PlacementSettings Settings(glm::ivec2 chunkTexel)
	{
		PlacementSettings s;
		s.chunkTexel = chunkTexel;
		s.chunkTexels = chunkTexels;
		s.mapOrigin = glm::vec2(0.0f);
		s.worldUnitsPerTexel = 4.0f;
		s.heightScale = 1024.0f;
		s.waterHeight = 0.1f;
		s.seed = 7;
		return s;
	}

	const std::vector<ModelInfo> models{ { "Tree", 3, Layer::TREES }, { "Bush", 1, Layer::SHRUBS }, { "Stone", 2, Layer::ROCKS },
		{ "Loading", 0, Layer::ROCKS } };

	const biome::Range anywhere{ -1.0f, 1.0f };

	//Plants of one biome: a tree of this density, a bush, a stone, a model still loading and a model that does not exist
	std::vector<std::vector<biome::Plant>> Plants(float treeDensity)
	{
		return { {
			{ "Tree", treeDensity, 1.0f, 2.0f, 30.0f, anywhere, 1.0f },
			{ "Bush", 0.2f, 1.0f, 1.0f, 40.0f, anywhere, 1.0f },
			{ "Stone", 0.2f, 2.0f, 3.0f, 70.0f, anywhere, 0.0f },
			{ "Loading", 0.5f, 1.0f, 1.0f, 70.0f, anywhere, 0.0f },
			{ "Unknown", 1.0f, 1.0f, 1.0f, 90.0f, anywhere, 0.0f },
		} };
	}

	float Flat(glm::vec2) { return 0.5f; }

	int CountLayer(const std::vector<Instance>& instances, Layer layer)
	{
		return static_cast<int>(std::count_if(instances.begin(), instances.end(), [&](const Instance& i) { return i.layer == layer; }));
	}
}

TEST_CASE("LayerOfKind: tree, shrub and rock; other kinds are shrubs")
{
	CHECK(LayerOfKind("tree") == Layer::TREES);
	CHECK(LayerOfKind("shrub") == Layer::SHRUBS);
	CHECK(LayerOfKind("rock") == Layer::ROCKS);
	CHECK(LayerOfKind("flower") == Layer::SHRUBS);
}

TEST_CASE("PlaceChunk: deterministic, inside its chunk, models and variants valid")
{
	const auto rulesOfBiome = Plants(0.5f);
	const TestMaps maps(glm::ivec2(256, -128), Flat, glm::vec2(0.0f), 0);
	std::vector<Instance> first, second;
	PlaceChunk(maps.maps, rulesOfBiome, models, Settings(glm::ivec2(256, -128)), first);
	PlaceChunk(maps.maps, rulesOfBiome, models, Settings(glm::ivec2(256, -128)), second);
	REQUIRE(!first.empty());
	REQUIRE(first.size() == second.size());
	int trees = 0;
	for (size_t i = 0; i < first.size(); i++) {
		const Instance& a = first[i];
		CHECK(a.position == second[i].position);
		CHECK(a.position.x >= 256.0f * 4.0f);
		CHECK(a.position.x < 384.0f * 4.0f);
		CHECK(a.position.z >= -128.0f * 4.0f);
		CHECK(a.position.z < 0.0f);
		CHECK(a.position.y == doctest::Approx(0.5f));
		REQUIRE(a.model >= 0);
		REQUIRE(a.model < 3);	//never the model without variants
		CHECK(a.variant < models[a.model].variants);
		CHECK(a.layer == models[a.model].layer);
		if (a.layer == Layer::TREES) {
			trees++;
			CHECK(a.model == 0);
			CHECK(a.scale >= unitsPerMetre * 1.0f);
			CHECK(a.scale <= unitsPerMetre * 2.0f);
		}
	}
	//256 tree cells at one half, times the clustering factor (0.15 to 1.85)
	CHECK(trees > 15);
	CHECK(trees < 240);
}

TEST_CASE("PlaceChunk: neighbouring chunks share no cell and repeat the same cells")
{
	const auto rulesOfBiome = Plants(0.6f);
	const auto wavy = [](glm::vec2 p) { return 0.5f + 0.01f * std::sin(p.x * 0.05f) * std::cos(p.y * 0.07f); };
	std::vector<Instance> left, right, again;
	PlaceChunk(TestMaps(glm::ivec2(0, 0), wavy, glm::vec2(0.0f), 0).maps, rulesOfBiome, models, Settings(glm::ivec2(0, 0)), left);
	PlaceChunk(TestMaps(glm::ivec2(128, 0), wavy, glm::vec2(0.0f), 0).maps, rulesOfBiome, models, Settings(glm::ivec2(128, 0)), right);
	std::set<std::pair<float, float>> positions;
	for (const Instance& i : left) {
		positions.insert({ i.position.x, i.position.z });
		CHECK(i.position.x < 512.0f);
	}
	for (const Instance& i : right) {
		CHECK(i.position.x >= 512.0f);
		CHECK(positions.count({ i.position.x, i.position.z }) == 0);
	}
	//Another seed moves everything
	PlacementSettings other = Settings(glm::ivec2(0, 0));
	other.seed = 8;
	PlaceChunk(TestMaps(glm::ivec2(0, 0), wavy, glm::vec2(0.0f), 0).maps, rulesOfBiome, models, other, again);
	REQUIRE(!again.empty());
	CHECK(again[0].position != left[0].position);
}

TEST_CASE("PlaceChunk: nothing under water, nothing beyond the slope of a plant, nothing without plants")
{
	const auto rules = Plants(0.8f);
	std::vector<Instance> out;
	PlaceChunk(TestMaps(glm::ivec2(0, 0), [](glm::vec2) { return 0.05f; }, glm::vec2(0.0f), 0).maps, rules, models, Settings(glm::ivec2(0, 0)), out);
	CHECK(out.empty());

	//Slope atan(0.001 * 1024) = 45.7 degrees: only the stones (70) stand
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.001f, 0.0f), 0).maps, rules, models, Settings(glm::ivec2(0, 0)), out);
	REQUIRE(!out.empty());
	for (const Instance& i : out) {
		CHECK(i.layer == Layer::ROCKS);
	}

	out.clear();
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.0f), 0).maps, { {} }, models, Settings(glm::ivec2(0, 0)), out);
	CHECK(out.empty());
	//A biome id beyond the table
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.0f), 3).maps, rules, models, Settings(glm::ivec2(0, 0)), out);
	CHECK(out.empty());

	PlacementSettings none = Settings(glm::ivec2(0, 0));
	none.density = 0.0f;
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.0f), 0).maps, rules, models, none, out);
	CHECK(out.empty());
}

TEST_CASE("PlaceChunk: a plant grows only in its elevation range, and plants that cannot grow leave their share to the others")
{
	//Height 0.5 over water at 0.1: elevation (0.5 - 0.1) / 0.9 = 0.44
	//Both rocks; the first one would take every cell, but it grows only above this land
	std::vector<std::vector<biome::Plant>> plants{ {
		{ "Loading", 1.0f, 1.0f, 1.0f, 90.0f, biome::Range{ 0.6f, 1.0f }, 0.0f },
		{ "Stone", 0.5f, 1.0f, 1.0f, 90.0f, biome::Range{ 0.4f, 0.5f }, 0.0f },
	} };
	std::vector<Instance> out;
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.0f), 0).maps, plants, models, Settings(glm::ivec2(0, 0)), out);
	//256 stone cells at one half
	const int stones = CountLayer(out, Layer::ROCKS);
	CHECK(stones > 96);
	CHECK(stones < 160);

	//The stone below its range too: nothing
	plants[0][1].elevation = biome::Range{ 0.5f, 1.0f };
	out.clear();
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.0f), 0).maps, plants, models, Settings(glm::ivec2(0, 0)), out);
	CHECK(out.empty());
}

TEST_CASE("PlaceChunk: without clustering a plant fills its density evenly; a model still loading keeps its cells empty")
{
	//Densities 0.25 + 0.25: the loading model takes its quarter of the cells without placing anything there
	const std::vector<std::vector<biome::Plant>> withLoading{ {
		{ "Loading", 0.25f, 1.0f, 1.0f, 90.0f, anywhere, 0.0f },
		{ "Stone", 0.25f, 1.0f, 1.0f, 90.0f, anywhere, 0.0f },
	} };
	const std::vector<std::vector<biome::Plant>> stoneOnly{ { { "Stone", 0.25f, 1.0f, 1.0f, 90.0f, anywhere, 0.0f } } };
	std::vector<Instance> loading, alone;
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.0f), 0).maps, withLoading, models, Settings(glm::ivec2(0, 0)), loading);
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.0f), 0).maps, stoneOnly, models, Settings(glm::ivec2(0, 0)), alone);
	//256 cells at a quarter each time, but in other cells: the stones moved behind the loading model
	CHECK(CountLayer(loading, Layer::ROCKS) > 40);
	CHECK(CountLayer(loading, Layer::ROCKS) < 90);
	CHECK(CountLayer(alone, Layer::ROCKS) > 40);
	CHECK(CountLayer(alone, Layer::ROCKS) < 90);
	for (const Instance& i : loading) {
		CHECK(i.model == 2);
	}
}

TEST_CASE("PlantCluster: clustering 0 is 1 everywhere, 1 is the cluster factor")
{
	CHECK(PlantCluster(1.7f, 0.0f) == 1.0f);
	CHECK(PlantCluster(1.7f, 1.0f) == doctest::Approx(1.7f));
	CHECK(PlantCluster(0.2f, 0.5f) == doctest::Approx(0.6f));
}

TEST_CASE("ClusterFactor stays in its range and averages about 1")
{
	double sum = 0.0;
	int count = 0;
	for (int y = 0; y < 200; y++) {
		for (int x = 0; x < 200; x++) {
			const float f = ClusterFactor(glm::vec2(x * 7.3f, y * 5.1f), 3);
			CHECK(f >= 0.15f);
			CHECK(f <= 1.85f);
			sum += f;
			count++;
		}
	}
	CHECK(sum / count == doctest::Approx(1.0).epsilon(0.1));
}

TEST_CASE("Default plants: land biomes have them, the sea has none, densities of a biome sum to at most 1, valid ranges")
{
	const std::vector<biome::Biome> table = biome::DefaultBiomes();
	for (const biome::Biome& b : table) {
		CAPTURE(b.name);
		CHECK(b.plants == biome::DefaultPlants(b.name));
		CHECK(b.plants.size() <= biome::maxPlants);
		//Sum over all layers, so each layer sums to at most 1 as well
		float sum = 0.0f;
		for (const biome::Plant& p : b.plants) {
			sum += p.density;
			CHECK(p.density > 0.0f);
			CHECK(p.minScale > 0.0f);
			CHECK(p.minScale <= p.maxScale);
			CHECK(p.clustering >= 0.0f);
			CHECK(p.clustering <= 1.0f);
		}
		CHECK(sum <= 1.0f);
		const bool underwater = b.ranges[biome::ELEVATION].max <= 0.0f;
		CHECK(b.plants.empty() == underwater);
	}
	CHECK(biome::DefaultPlants("No such biome").empty());
}

TEST_CASE("PlaceChunk: a coarse chunk places the same trees as the fine chunks it covers, and only the layers of the mask")
{
	//Level 1: a chunk of 256 texels sampled every 2nd texel, over the four fine chunks of 128 texels
	const glm::ivec2 origin(256, -512);
	std::vector<Instance> fine;
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 2; x++) {
			const glm::ivec2 chunk = origin + glm::ivec2(x, y) * chunkTexels;
			const TestMaps maps(chunk, Flat, glm::vec2(0.0f), 0);
			PlacementSettings s = Settings(chunk);
			s.layerMask = 1u << static_cast<int>(Layer::TREES);
			PlaceChunk(maps.maps, Plants(0.5f), models, s, fine);
		}
	}
	TestMaps coarse(origin, Flat, glm::vec2(0.0f), 0);
	coarse.maps.step = 2;
	coarse.maps.firstTexel = origin - 2;
	PlacementSettings s = Settings(origin);
	s.chunkTexels = 2 * chunkTexels;
	s.layerMask = 1u << static_cast<int>(Layer::TREES);
	std::vector<Instance> far;
	PlaceChunk(coarse.maps, Plants(0.5f), models, s, far);

	REQUIRE(!fine.empty());
	CHECK(CountLayer(fine, Layer::SHRUBS) == 0);
	CHECK(CountLayer(fine, Layer::ROCKS) == 0);
	REQUIRE(far.size() == fine.size());
	auto key = [](const Instance& i) { return std::make_pair(i.position.x, i.position.z); };
	std::set<std::pair<float, float>> a, b;
	for (const Instance& i : fine) {
		a.insert(key(i));
	}
	for (const Instance& i : far) {
		b.insert(key(i));
	}
	CHECK(a == b);
}

TEST_CASE("Morton: aligned squares of blocks are contiguous ranges")
{
	CHECK(Morton(0, 0) == 0);
	CHECK(Morton(1, 0) == 1);
	CHECK(Morton(0, 1) == 2);
	CHECK(Morton(1, 1) == 3);
	CHECK(Morton(2, 0) == 4);
	//The 2 x 2 square at (2, 2) of a 4 x 4 grid is 12..15
	std::set<uint32_t> square;
	for (uint32_t y = 2; y < 4; y++) {
		for (uint32_t x = 2; x < 4; x++) {
			square.insert(Morton(x, y));
		}
	}
	CHECK(square == std::set<uint32_t>{ 12, 13, 14, 15 });
}

TEST_CASE("SortByBlock: instances grouped by block in Morton order, starts cover them all")
{
	const glm::ivec2 chunk(128, 0);
	const glm::vec2 mapOrigin(-100.0f, 40.0f);
	const float units = 4.0f;
	std::vector<Instance> instances;
	//Blocks of 32 texels, 4 x 4 of them; one instance in the middle of every block, listed backwards
	for (int y = 3; y >= 0; y--) {
		for (int x = 3; x >= 0; x--) {
			Instance i;
			const glm::vec2 texel = glm::vec2(chunk) + glm::vec2(x * 32 + 16, y * 32 + 16);
			i.position = glm::vec3(mapOrigin.x + texel.x * units, 0.0f, mapOrigin.y + texel.y * units);
			i.variant = x + 4 * y;
			instances.push_back(i);
		}
	}
	const std::vector<uint32_t> starts = SortByBlock(instances, mapOrigin, units, chunk, 32, 4);
	REQUIRE(starts.size() == 17);
	CHECK(starts.front() == 0);
	CHECK(starts.back() == 16);
	for (uint32_t m = 0; m < 16; m++) {
		REQUIRE(starts[m + 1] - starts[m] == 1);
		const Instance& i = instances[starts[m]];
		CHECK(Morton(static_cast<uint32_t>(i.variant % 4), static_cast<uint32_t>(i.variant / 4)) == m);
	}
}
