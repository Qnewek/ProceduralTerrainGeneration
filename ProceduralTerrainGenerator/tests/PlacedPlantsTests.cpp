#include "doctest/doctest.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>

#include "PlacedPlants.h"

namespace {
	placedplants::Brush OakBrush(glm::vec2 centre, int count)
	{
		placedplants::Brush brush;
		brush.centre = centre;
		brush.count = count;
		brush.models = { "oak" };
		brush.variants = { 3 };
		return brush;
	}

	placedplants::Plant PlantAt(glm::vec2 position, uint16_t model = 0)
	{
		placedplants::Plant p;
		p.position = position;
		p.model = model;
		return p;
	}
}

TEST_CASE("Scatter with count 1 puts the plant at the centre")
{
	placedplants::Layer layer;
	const size_t added = placedplants::Scatter(layer, OakBrush(glm::vec2(5.0f, 7.0f), 1), glm::vec2(100.0f));
	REQUIRE(added == 1);
	REQUIRE(layer.plants.size() == 1);
	const placedplants::Plant& p = layer.plants[0];
	CHECK(p.position == glm::vec2(5.0f, 7.0f));
	CHECK(layer.palette == std::vector<std::string>{ "oak" });
	CHECK(p.model == 0);
	CHECK(p.variant < 3);
	CHECK(p.scale >= 0.8f);
	CHECK(p.scale <= 1.2f);
}

TEST_CASE("Scatter keeps the spacing and stays on the map")
{
	placedplants::Layer layer;
	placedplants::Brush brush = OakBrush(glm::vec2(10.0f), 200);
	brush.radius = 30.0f;
	brush.spacing = 3.0f;
	brush.seed = 1;
	const size_t first = placedplants::Scatter(layer, brush, glm::vec2(50.0f));
	CHECK(first > 0);
	CHECK(first < 200);
	brush.seed = 2;
	placedplants::Scatter(layer, brush, glm::vec2(50.0f));
	for (size_t i = 0; i < layer.plants.size(); i++) {
		const glm::vec2 a = layer.plants[i].position;
		CHECK(a.x >= 0.0f);
		CHECK(a.y >= 0.0f);
		CHECK(a.x <= 50.0f);
		CHECK(a.y <= 50.0f);
		for (size_t j = i + 1; j < layer.plants.size(); j++) {
			CHECK(glm::distance(a, layer.plants[j].position) >= 3.0f);
		}
	}
}

TEST_CASE("Scatter is deterministic")
{
	placedplants::Brush brush = OakBrush(glm::vec2(40.0f), 30);
	brush.radius = 20.0f;
	brush.seed = 7;
	placedplants::Layer a, b;
	placedplants::Scatter(a, brush, glm::vec2(100.0f));
	placedplants::Scatter(b, brush, glm::vec2(100.0f));
	CHECK(a == b);
	CHECK_FALSE(a.plants.empty());
}

TEST_CASE("Scatter spreads over several models")
{
	placedplants::Brush brush;
	brush.centre = glm::vec2(50.0f);
	brush.radius = 40.0f;
	brush.count = 300;
	brush.spacing = 0.0f;
	brush.models = { "a", "b", "c" };
	brush.variants = { 1, 2, 1 };
	placedplants::Layer layer;
	placedplants::Scatter(layer, brush, glm::vec2(100.0f));
	std::set<std::string> used;
	for (const placedplants::Plant& p : layer.plants) {
		REQUIRE(p.model < layer.palette.size());
		used.insert(layer.palette[p.model]);
	}
	CHECK(used.size() == 3);
}

TEST_CASE("Scatter without models places nothing")
{
	placedplants::Brush brush = OakBrush(glm::vec2(5.0f), 10);
	brush.models.clear();
	brush.variants.clear();
	placedplants::Layer layer;
	CHECK(placedplants::Scatter(layer, brush, glm::vec2(100.0f)) == 0);
	CHECK(layer.plants.empty());
}

TEST_CASE("Erase removes plants inside the radius")
{
	placedplants::Layer layer;
	layer.palette = { "oak" };
	layer.plants = { PlantAt(glm::vec2(0.0f)), PlantAt(glm::vec2(4.0f, 0.0f)), PlantAt(glm::vec2(10.0f, 0.0f)) };
	CHECK(placedplants::Erase(layer, glm::vec2(0.0f), 5.0f) == 2);
	REQUIRE(layer.plants.size() == 1);
	CHECK(layer.plants[0].position == glm::vec2(10.0f, 0.0f));
}

TEST_CASE("ToPlaced stands plants on the heights")
{
	HeightField units(3, 3);
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 3; x++) {
			units.At(x, y) = static_cast<float>(y);
		}
	}
	placedplants::Layer layer;
	layer.palette = { "oak" };
	layer.plants = { PlantAt(glm::vec2(2.0f, 1.0f)) };
	const std::vector<vegexport::Model> models = { { "oak", "tree", "", 2, {}, {}, {} } };
	const std::vector<vegexport::Placed> placed = placedplants::ToPlaced(layer, units, 10.0f, 2.0f, models);
	REQUIRE(placed.size() == 1);
	CHECK(placed[0].position.x == doctest::Approx(2.0f));
	CHECK(placed[0].position.z == doctest::Approx(1.0f));
	CHECK(placed[0].position.y == doctest::Approx(5.0f));
}

TEST_CASE("ToPlaced skips unknown and unloaded models")
{
	HeightField units(4, 4);
	placedplants::Layer layer;
	layer.palette = { "oak", "gone", "unloaded" };
	layer.plants = { PlantAt(glm::vec2(1.0f), 0), PlantAt(glm::vec2(1.0f), 1), PlantAt(glm::vec2(1.0f), 2) };
	layer.plants[0].variant = 5;
	const std::vector<vegexport::Model> models = { { "rock", "rock", "", 1, {}, {}, {} }, { "oak", "tree", "", 2, {}, {}, {} },
		{ "unloaded", "shrub", "", 0, {}, {}, {} } };
	const std::vector<vegexport::Placed> placed = placedplants::ToPlaced(layer, units, 1.0f, 1.0f, models);
	REQUIRE(placed.size() == 1);
	CHECK(placed[0].model == 1);
	CHECK(placed[0].variant == 1);
	CHECK(placed[0].layer == vegetation::Layer::TREES);
}

TEST_CASE("ToPlaced skips plants outside the map")
{
	HeightField units(4, 4);
	placedplants::Layer layer;
	layer.palette = { "oak" };
	layer.plants = { PlantAt(glm::vec2(3.5f, 1.0f)), PlantAt(glm::vec2(3.0f, 3.0f)) };
	const std::vector<vegexport::Model> models = { { "oak", "tree", "", 1, {}, {}, {} } };
	const std::vector<vegexport::Placed> placed = placedplants::ToPlaced(layer, units, 1.0f, 1.0f, models);
	REQUIRE(placed.size() == 1);
	CHECK(placed[0].position.x == doctest::Approx(3.0f));
}

TEST_CASE("Pack and Unpack round trip")
{
	placedplants::Layer layer;
	layer.palette = { "oak", "pine" };
	layer.plants = { PlantAt(glm::vec2(1.5f, -2.0f), 1), PlantAt(glm::vec2(300.25f, 7.0f), 0) };
	layer.plants[0].yaw = 1.25f;
	layer.plants[0].scale = 0.9f;
	layer.plants[0].variant = 3;
	const std::vector<uint8_t> bytes = placedplants::Pack(layer);
	CHECK(bytes.size() == 2 * placedplants::plantBytes);
	placedplants::Layer read;
	read.palette = layer.palette;
	std::string error;
	REQUIRE_MESSAGE(placedplants::Unpack(bytes.data(), bytes.size(), read, error), error);
	CHECK(read == layer);
}

TEST_CASE("Unpack rejects damaged data")
{
	placedplants::Layer layer;
	layer.palette = { "oak" };
	layer.plants = { PlantAt(glm::vec2(1.0f)) };
	const std::vector<uint8_t> good = placedplants::Pack(layer);
	auto rejects = [&](std::vector<uint8_t> bytes) {
		placedplants::Layer read;
		read.palette = { "oak" };
		read.plants = { PlantAt(glm::vec2(9.0f)) };
		std::string error;
		const bool ok = placedplants::Unpack(bytes.data(), bytes.size(), read, error);
		return !ok && !error.empty() && read.plants.size() == 1 && read.plants[0].position == glm::vec2(9.0f);
	};
	CHECK(rejects(std::vector<uint8_t>(good.begin(), good.end() - 1)));
	std::vector<uint8_t> model = good;
	model[16] = 1;
	CHECK(rejects(model));
	std::vector<uint8_t> nan = good;
	const float notANumber = std::numeric_limits<float>::quiet_NaN();
	std::memcpy(nan.data(), &notANumber, sizeof(float));
	CHECK(rejects(nan));
	std::vector<uint8_t> scale = good;
	const float zero = 0.0f;
	std::memcpy(scale.data() + 12, &zero, sizeof(float));
	CHECK(rejects(scale));
}
