#include "doctest/doctest.h"

#include <algorithm>
#include <numeric>

#include "MapEdit.h"

namespace {
	HeightField Filled(int width, int height, float value)
	{
		HeightField map(width, height);
		for (size_t i = 0; i < map.Size(); i++) {
			map[i] = value;
		}
		return map;
	}

	float Sum(const HeightField& map)
	{
		return std::accumulate(map.Data(), map.Data() + map.Size(), 0.0f);
	}

	mapedit::Step MapStep(const char* label, float value)
	{
		mapedit::Step step;
		step.label = label;
		step.map = Filled(4, 4, value);
		return step;
	}
}

TEST_CASE("Smooth with radius 0 or strength 0 keeps the map")
{
	HeightField map = Filled(7, 5, 0.0f);
	map.At(3, 2) = 10.0f;
	const HeightField before = map;
	mapedit::Smooth(map, 0, 1.0f);
	CHECK(std::equal(map.Data(), map.Data() + map.Size(), before.Data()));
	mapedit::Smooth(map, 2, 0.0f);
	CHECK(std::equal(map.Data(), map.Data() + map.Size(), before.Data()));
}

TEST_CASE("Smooth keeps a flat map flat and the mean of a symmetric bump")
{
	HeightField flat = Filled(7, 5, 3.5f);
	mapedit::Smooth(flat, 2, 1.0f);
	for (size_t i = 0; i < flat.Size(); i++) {
		CHECK(flat[i] == doctest::Approx(3.5f).epsilon(1e-5));
	}

	HeightField bump = Filled(9, 7, 0.0f);
	bump.At(4, 3) = 10.0f;
	mapedit::Smooth(bump, 1, 1.0f);
	CHECK(*std::max_element(bump.Data(), bump.Data() + bump.Size()) < 10.0f);
	CHECK(bump.At(4, 3) > 0.0f);
	CHECK(bump.At(3, 3) > 0.0f);
	CHECK(bump.At(4, 2) > 0.0f);
	CHECK(Sum(bump) <= doctest::Approx(10.0f).epsilon(1e-4));
	//Radius 1 on a 9 x 7 map does not reach the corners
	CHECK(bump.At(0, 0) == 0.0f);
}

TEST_CASE("Smooth strength 0.5 lies halfway")
{
	HeightField source = Filled(9, 7, 0.0f);
	source.At(4, 3) = 10.0f;
	source.At(1, 5) = -4.0f;
	HeightField full = source;
	HeightField half = source;
	mapedit::Smooth(full, 2, 1.0f);
	mapedit::Smooth(half, 2, 0.5f);
	for (size_t i = 0; i < source.Size(); i++) {
		CHECK(half[i] == doctest::Approx(0.5f * (source[i] + full[i])).epsilon(1e-5));
	}
}

TEST_CASE("Rescale multiplies and shifts")
{
	HeightField map = Filled(3, 2, 2.0f);
	mapedit::Rescale(map, 1.5f, -10.0f);
	for (size_t i = 0; i < map.Size(); i++) {
		CHECK(map[i] == doctest::Approx(-7.0f));
	}
}

TEST_CASE("Erosion scale makes 1 m pixels TerrainGen units")
{
	CHECK(mapedit::ErosionScale(1.0) == doctest::Approx(1.0f / 256.0f));
	CHECK(mapedit::ErosionScale(30.0) == doctest::Approx(1.0f / 7680.0f));
}

TEST_CASE("History undo and redo swap the map")
{
	mapedit::History history(1 << 20);
	HeightField current = Filled(4, 4, 2.0f);
	std::optional<float> sea = 5.0f;
	mapedit::Step before = MapStep("Smooth", 1.0f);
	before.seaLevel = 5.0f;
	REQUIRE(history.Push(std::move(before)));
	CHECK(history.CanUndo());
	CHECK(!history.CanRedo());
	CHECK(history.UndoLabel() == "Smooth");

	REQUIRE(history.Undo(current, sea));
	CHECK(current[0] == 1.0f);
	CHECK(!history.CanUndo());
	CHECK(history.CanRedo());
	CHECK(history.RedoLabel() == "Smooth");

	REQUIRE(history.Redo(current, sea));
	CHECK(current[0] == 2.0f);
	CHECK(history.CanUndo());
	CHECK(!history.Redo(current, sea));
}

TEST_CASE("History step without a map keeps the map")
{
	mapedit::History history(1 << 20);
	HeightField current = Filled(4, 4, 2.0f);
	std::optional<float> sea = 12.0f;
	mapedit::Step before;
	before.label = "Sea level";
	before.seaLevel = 3.0f;
	REQUIRE(history.Push(std::move(before)));
	CHECK(history.Bytes() == 0);

	REQUIRE(history.Undo(current, sea));
	REQUIRE(sea.has_value());
	CHECK(*sea == 3.0f);
	CHECK(current.GetWidth() == 4);
	CHECK(current[0] == 2.0f);

	REQUIRE(history.Redo(current, sea));
	CHECK(*sea == 12.0f);
	CHECK(current[0] == 2.0f);
}

TEST_CASE("Push clears redo")
{
	mapedit::History history(1 << 20);
	HeightField current = Filled(4, 4, 2.0f);
	std::optional<float> sea;
	history.Push(MapStep("1", 1.0f));
	history.Undo(current, sea);
	REQUIRE(history.CanRedo());
	history.Push(MapStep("2", 3.0f));
	CHECK(!history.CanRedo());
	CHECK(history.UndoLabel() == "2");
}

TEST_CASE("History drops the oldest steps over the limit")
{
	const size_t step = 16 * sizeof(float);
	mapedit::History history(2 * step);
	history.Push(MapStep("1", 1.0f));
	history.Push(MapStep("2", 2.0f));
	history.Push(MapStep("3", 3.0f));
	CHECK(history.Bytes() == 2 * step);

	HeightField current = Filled(4, 4, 4.0f);
	std::optional<float> sea;
	REQUIRE(history.Undo(current, sea));
	CHECK(current[0] == 3.0f);
	REQUIRE(history.Undo(current, sea));
	CHECK(current[0] == 2.0f);
	//Step "1" was dropped
	CHECK(!history.Undo(current, sea));
}

TEST_CASE("A step larger than the limit leaves no history")
{
	mapedit::History history(10);
	CHECK(!history.Push(MapStep("big", 1.0f)));
	CHECK(!history.CanUndo());
	CHECK(history.Bytes() == 0);
}

TEST_CASE("Lowering the limit drops steps")
{
	const size_t step = 16 * sizeof(float);
	mapedit::History history(4 * step);
	for (const char* label : { "1", "2", "3" }) {
		history.Push(MapStep(label, 1.0f));
	}
	HeightField current = Filled(4, 4, 4.0f);
	std::optional<float> sea;
	history.Undo(current, sea);
	REQUIRE(history.Bytes() == 3 * step);
	history.SetLimit(step);
	CHECK(history.Limit() == step);
	CHECK(history.Bytes() <= step);
	//The older undo steps go first, the redo step stays
	CHECK(!history.CanUndo());
	CHECK(history.CanRedo());
}
