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

	const std::vector<ModelInfo> models{ { "Tree", 3 }, { "Bush", 1 }, { "Stone", 2 }, { "Missing", 0 } };

	BiomeRules Rules(float treeProbability)
	{
		return { "Forest", {
			{ "Tree", Layer::TREES, treeProbability, 1.0f, 2.0f, 30.0f },
			{ "Bush", Layer::SHRUBS, 0.2f, 1.0f, 1.0f, 40.0f },
			{ "Stone", Layer::ROCKS, 0.2f, 2.0f, 3.0f, 70.0f },
			{ "Missing", Layer::ROCKS, 0.5f, 1.0f, 1.0f, 70.0f },
		} };
	}

	float Flat(glm::vec2) { return 0.5f; }
}

TEST_CASE("PlaceChunk: deterministic, inside its chunk, models and variants valid")
{
	const BiomeRules rules = Rules(0.5f);
	const std::vector<const BiomeRules*> rulesOfBiome{ &rules };
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
	const BiomeRules rules = Rules(0.6f);
	const std::vector<const BiomeRules*> rulesOfBiome{ &rules };
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

TEST_CASE("PlaceChunk: nothing under water, nothing beyond the slope of a species, nothing without rules")
{
	const BiomeRules rules = Rules(0.8f);
	std::vector<Instance> out;
	PlaceChunk(TestMaps(glm::ivec2(0, 0), [](glm::vec2) { return 0.05f; }, glm::vec2(0.0f), 0).maps, { &rules }, models, Settings(glm::ivec2(0, 0)), out);
	CHECK(out.empty());

	//Slope atan(0.001 * 1024) = 45.7 degrees: only the stones (70) stand
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.001f, 0.0f), 0).maps, { &rules }, models, Settings(glm::ivec2(0, 0)), out);
	REQUIRE(!out.empty());
	for (const Instance& i : out) {
		CHECK(i.layer == Layer::ROCKS);
	}

	out.clear();
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.0f), 0).maps, { nullptr }, models, Settings(glm::ivec2(0, 0)), out);
	CHECK(out.empty());
	//A biome id beyond the rules
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.0f), 3).maps, { &rules }, models, Settings(glm::ivec2(0, 0)), out);
	CHECK(out.empty());

	PlacementSettings none = Settings(glm::ivec2(0, 0));
	none.density = 0.0f;
	PlaceChunk(TestMaps(glm::ivec2(0, 0), Flat, glm::vec2(0.0f), 0).maps, { &rules }, models, none, out);
	CHECK(out.empty());
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

TEST_CASE("DefaultRules name biomes of the default table, probabilities of a layer sum to at most 1")
{
	const std::vector<biome::Biome> table = biome::DefaultBiomes();
	for (const BiomeRules& rules : DefaultRules()) {
		const bool known = std::any_of(table.begin(), table.end(), [&](const biome::Biome& b) { return b.name == rules.biome; });
		CHECK_MESSAGE(known, rules.biome);
		float sums[layerCount] = {};
		for (const Species& s : rules.species) {
			sums[static_cast<int>(s.layer)] += s.probability;
			CHECK(s.minScale <= s.maxScale);
		}
		for (const float sum : sums) {
			CHECK(sum <= 1.0f);
		}
	}
}
