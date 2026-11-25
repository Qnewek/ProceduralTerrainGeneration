#include "doctest/doctest.h"

#include <algorithm>
#include <numeric>

#include "BiomePaint.h"
#include "MapEdit.h"
#include "PlacedPlants.h"

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
	biomepaint::Layer paint;
	placedplants::Layer plants;
	mapedit::Step before = MapStep("Smooth", 1.0f);
	before.seaLevel = 5.0f;
	REQUIRE(history.Push(std::move(before)));
	CHECK(history.CanUndo());
	CHECK(!history.CanRedo());
	CHECK(history.UndoLabel() == "Smooth");

	REQUIRE(history.Undo(current, sea, paint, plants).has_value());
	CHECK(current[0] == 1.0f);
	CHECK(!history.CanUndo());
	CHECK(history.CanRedo());
	CHECK(history.RedoLabel() == "Smooth");

	REQUIRE(history.Redo(current, sea, paint, plants).has_value());
	CHECK(current[0] == 2.0f);
	CHECK(history.CanUndo());
	CHECK(!history.Redo(current, sea, paint, plants).has_value());
}

TEST_CASE("History restores placed plants only from their steps")
{
	mapedit::History history(1 << 20);
	HeightField current = Filled(4, 4, 2.0f);
	std::optional<float> sea;
	biomepaint::Layer paint;
	placedplants::Layer a, b;
	a.palette = { "oak" };
	a.plants.resize(1);
	b.palette = { "oak", "pine" };
	b.plants.resize(3);
	REQUIRE(history.Push(MapStep("Smooth", 1.0f)));
	mapedit::Step plantStep;
	plantStep.label = "Place plants";
	plantStep.plants = a;
	REQUIRE(history.Push(std::move(plantStep)));
	placedplants::Layer plants = b;

	const std::optional<mapedit::Restored> undone = history.Undo(current, sea, paint, plants);
	REQUIRE(undone.has_value());
	CHECK(undone->plants);
	CHECK_FALSE(undone->map);
	CHECK(plants == a);
	const std::optional<mapedit::Restored> map = history.Undo(current, sea, paint, plants);
	REQUIRE(map.has_value());
	CHECK_FALSE(map->plants);
	CHECK(map->map);
	CHECK(plants == a);

	REQUIRE(history.Redo(current, sea, paint, plants).has_value());
	REQUIRE(history.Redo(current, sea, paint, plants).has_value());
	CHECK(plants == b);
}

TEST_CASE("History step without a map keeps the map")
{
	mapedit::History history(1 << 20);
	HeightField current = Filled(4, 4, 2.0f);
	std::optional<float> sea = 12.0f;
	biomepaint::Layer paint;
	placedplants::Layer plants;
	mapedit::Step before;
	before.label = "Sea level";
	before.seaLevel = 3.0f;
	REQUIRE(history.Push(std::move(before)));
	CHECK(history.Bytes() == 0);

	REQUIRE(history.Undo(current, sea, paint, plants).has_value());
	REQUIRE(sea.has_value());
	CHECK(*sea == 3.0f);
	CHECK(current.GetWidth() == 4);
	CHECK(current[0] == 2.0f);

	REQUIRE(history.Redo(current, sea, paint, plants).has_value());
	CHECK(*sea == 12.0f);
	CHECK(current[0] == 2.0f);
}

TEST_CASE("Push clears redo")
{
	mapedit::History history(1 << 20);
	HeightField current = Filled(4, 4, 2.0f);
	std::optional<float> sea;
	biomepaint::Layer paint;
	placedplants::Layer plants;
	history.Push(MapStep("1", 1.0f));
	history.Undo(current, sea, paint, plants);
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
	biomepaint::Layer paint;
	placedplants::Layer plants;
	REQUIRE(history.Undo(current, sea, paint, plants).has_value());
	CHECK(current[0] == 3.0f);
	REQUIRE(history.Undo(current, sea, paint, plants).has_value());
	CHECK(current[0] == 2.0f);
	//Step "1" was dropped
	CHECK(!history.Undo(current, sea, paint, plants).has_value());
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
	biomepaint::Layer paint;
	placedplants::Layer plants;
	history.Undo(current, sea, paint, plants);
	REQUIRE(history.Bytes() == 3 * step);
	history.SetLimit(step);
	CHECK(history.Limit() == step);
	CHECK(history.Bytes() <= step);
	//The older undo steps go first, the redo step stays
	CHECK(!history.CanUndo());
	CHECK(history.CanRedo());
}

TEST_CASE("Paint step restores only the paint")
{
	mapedit::History history(1 << 20);
	HeightField current = Filled(4, 4, 2.0f);
	std::optional<float> sea = 7.0f;
	biomepaint::Layer before = biomepaint::Make(4, 4);
	before.palette = { "A" };
	before.texels[5] = static_cast<uint16_t>((200 << 8) | 1);
	biomepaint::Layer paint = biomepaint::Make(4, 4);
	placedplants::Layer plants;
	paint.palette = { "B" };
	mapedit::Step step;
	step.label = "Paint biomes";
	step.seaLevel = 7.0f;
	step.paint = before;
	REQUIRE(history.Push(std::move(step)));

	const std::optional<mapedit::Restored> undone = history.Undo(current, sea, paint, plants);
	REQUIRE(undone.has_value());
	CHECK(!undone->map);
	CHECK(!undone->seaLevel);
	CHECK(undone->paint);
	CHECK(paint.palette == std::vector<std::string>{ "A" });
	CHECK(paint.texels == before.texels);
	CHECK(current[0] == 2.0f);
	REQUIRE(sea.has_value());
	CHECK(*sea == 7.0f);

	const std::optional<mapedit::Restored> redone = history.Redo(current, sea, paint, plants);
	REQUIRE(redone.has_value());
	CHECK(redone->paint);
	CHECK(paint.palette == std::vector<std::string>{ "B" });
	CHECK(!history.Redo(current, sea, paint, plants).has_value());
}

TEST_CASE("Paint counts in the history memory")
{
	mapedit::History history(1 << 20);
	mapedit::Step step;
	step.label = "Paint biomes";
	step.paint = biomepaint::Make(8, 6);
	const size_t bytes = step.paint->Bytes();
	REQUIRE(bytes == 8 * 6 * 2);
	REQUIRE(history.Push(std::move(step)));
	CHECK(history.Bytes() == bytes);
}

TEST_CASE("Sea step reports the sea level and the map step the map")
{
	mapedit::History history(1 << 20);
	HeightField current = Filled(4, 4, 2.0f);
	std::optional<float> sea = 12.0f;
	biomepaint::Layer paint;
	placedplants::Layer plants;
	mapedit::Step seaStep;
	seaStep.label = "Sea level";
	seaStep.seaLevel = 3.0f;
	REQUIRE(history.Push(std::move(seaStep)));
	const std::optional<mapedit::Restored> undone = history.Undo(current, sea, paint, plants);
	REQUIRE(undone.has_value());
	CHECK(undone->seaLevel);
	CHECK(!undone->map);
	CHECK(!undone->paint);

	mapedit::Step mapStep = MapStep("Smooth", 1.0f);
	mapStep.seaLevel = sea;
	REQUIRE(history.Push(std::move(mapStep)));
	const std::optional<mapedit::Restored> map = history.Undo(current, sea, paint, plants);
	REQUIRE(map.has_value());
	CHECK(map->map);
	CHECK(!map->seaLevel);
}

TEST_CASE("Paint step of another size keeps the history memory right")
{
	//The first stroke saves the empty layer (0x0); undo swaps the painted one into the step
	mapedit::History history(1 << 20);
	HeightField current = Filled(4, 4, 2.0f);
	std::optional<float> sea;
	biomepaint::Layer paint = biomepaint::Make(8, 6);
	placedplants::Layer plants;
	mapedit::Step step;
	step.label = "Paint biomes";
	step.paint = biomepaint::Layer();
	REQUIRE(history.Push(std::move(step)));
	CHECK(history.Bytes() == 0);

	REQUIRE(history.Undo(current, sea, paint, plants).has_value());
	CHECK(paint.texels.empty());
	CHECK(history.Bytes() == 8 * 6 * 2);

	//A new step drops the redo step and its bytes; the history keeps working
	mapedit::Step next;
	next.label = "Sea level";
	REQUIRE(history.Push(std::move(next)));
	CHECK(history.Bytes() == 0);
	CHECK(history.CanUndo());
	CHECK(history.UndoLabel() == "Sea level");
}

TEST_CASE("History: dropping the last step")
{
	mapedit::History history(1 << 20);
	REQUIRE(history.Push(MapStep("A", 1.0f)));
	const size_t bytesOfA = history.Bytes();
	REQUIRE(history.Push(MapStep("B", 2.0f)));
	history.DropLast();
	CHECK(history.CanUndo());
	CHECK(history.UndoLabel() == "A");
	CHECK(history.Bytes() == bytesOfA);
	history.DropLast();
	history.DropLast();
	CHECK_FALSE(history.CanUndo());
	CHECK(history.Bytes() == 0);
}
