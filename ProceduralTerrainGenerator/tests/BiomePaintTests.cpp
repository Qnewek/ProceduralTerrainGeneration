#include "doctest/doctest.h"

#include <string>

#include "BiomePaint.h"

namespace {
	constexpr int width = 40, height = 24;

	int SlotAt(const biomepaint::Layer& layer, int x, int y)
	{
		return layer.texels[static_cast<size_t>(y) * layer.width + x] & 0xFF;
	}

	int StrengthAt(const biomepaint::Layer& layer, int x, int y)
	{
		return layer.texels[static_cast<size_t>(y) * layer.width + x] >> 8;
	}

	biomepaint::Dab DabAt(glm::vec2 center, float radius, float hardness, float amount, uint8_t slot, bool erase = false)
	{
		biomepaint::Dab dab;
		dab.center = center;
		dab.radius = radius;
		dab.hardness = hardness;
		dab.amount = amount;
		dab.slot = slot;
		dab.erase = erase;
		return dab;
	}
}

TEST_CASE("Paint raises the strength up to full")
{
	biomepaint::Layer layer = biomepaint::Make(width, height);
	CHECK(layer.Empty());
	const uint8_t slot = biomepaint::SlotOf(layer, "Forest");
	REQUIRE(slot == 1);
	const glm::ivec4 rect = biomepaint::Apply(layer, DabAt({ 10.0f, 10.0f }, 4.0f, 1.0f, 0.3f, slot));
	for (int i = 0; i < 4; i++) {
		biomepaint::Apply(layer, DabAt({ 10.0f, 10.0f }, 4.0f, 1.0f, 0.3f, slot));
	}
	CHECK(SlotAt(layer, 10, 10) == 1);
	CHECK(StrengthAt(layer, 10, 10) == 255);
	CHECK(StrengthAt(layer, 20, 10) == 0);
	CHECK(SlotAt(layer, 20, 10) == 0);
	CHECK_FALSE(layer.Empty());
	//The changed rectangle covers the brush and stays inside the map
	CHECK(rect.x <= 6);
	CHECK(rect.y <= 6);
	CHECK(rect.x + rect.z >= 15);
	CHECK(rect.y + rect.w >= 15);
	CHECK(rect.x >= 0);
	CHECK(rect.y >= 0);
	CHECK(rect.x + rect.z <= width);
	CHECK(rect.y + rect.w <= height);
}

TEST_CASE("Painting another biome fades the first one out first")
{
	biomepaint::Layer layer = biomepaint::Make(width, height);
	const uint8_t first = biomepaint::SlotOf(layer, "Forest"), second = biomepaint::SlotOf(layer, "Desert");
	REQUIRE(second == 2);
	biomepaint::Apply(layer, DabAt({ 10.0f, 10.0f }, 4.0f, 1.0f, 1.0f, first));
	REQUIRE(StrengthAt(layer, 10, 10) == 255);
	biomepaint::Apply(layer, DabAt({ 10.0f, 10.0f }, 4.0f, 1.0f, 0.4f, second));
	CHECK(SlotAt(layer, 10, 10) == 1);
	CHECK(StrengthAt(layer, 10, 10) == 153);
	for (int i = 0; i < 3; i++) {
		biomepaint::Apply(layer, DabAt({ 10.0f, 10.0f }, 4.0f, 1.0f, 0.4f, second));
	}
	CHECK(SlotAt(layer, 10, 10) == 2);
	CHECK(StrengthAt(layer, 10, 10) > 0);
}

TEST_CASE("Eraser goes back to automatic")
{
	biomepaint::Layer layer = biomepaint::Make(width, height);
	const uint8_t slot = biomepaint::SlotOf(layer, "Forest");
	biomepaint::Apply(layer, DabAt({ 10.0f, 10.0f }, 4.0f, 1.0f, 1.0f, slot));
	REQUIRE_FALSE(layer.Empty());
	biomepaint::Apply(layer, DabAt({ 10.0f, 10.0f }, 6.0f, 1.0f, 1.0f, slot, true));
	CHECK(StrengthAt(layer, 10, 10) == 0);
	CHECK(SlotAt(layer, 10, 10) == 0);
	CHECK(layer.Empty());
}

TEST_CASE("Soft edge is weaker than the centre")
{
	biomepaint::Layer layer = biomepaint::Make(width, height);
	const uint8_t slot = biomepaint::SlotOf(layer, "Forest");
	biomepaint::Apply(layer, DabAt({ 10.0f, 10.0f }, 8.0f, 0.0f, 0.5f, slot));
	CHECK(StrengthAt(layer, 10, 10) > StrengthAt(layer, 15, 10));
	CHECK(StrengthAt(layer, 15, 10) > 0);
	CHECK(StrengthAt(layer, 19, 10) == 0);
}

TEST_CASE("Slow painting still adds up")
{
	//Speed 0.1 /s at 120 frames a second: under half a step of the strength per dab, two seconds = about 0.2 * 255
	biomepaint::Layer layer = biomepaint::Make(width, height);
	const uint8_t slot = biomepaint::SlotOf(layer, "Forest");
	for (uint32_t i = 0; i < 240; i++) {
		biomepaint::Dab dab = DabAt({ 10.0f, 10.0f }, 8.0f, 0.0f, 0.1f / 120.0f, slot);
		dab.seed = i;
		biomepaint::Apply(layer, dab);
	}
	CHECK(StrengthAt(layer, 10, 10) >= 40);
	CHECK(StrengthAt(layer, 10, 10) <= 62);
	//The soft edge too, weaker
	CHECK(StrengthAt(layer, 15, 10) > 0);
	CHECK(StrengthAt(layer, 15, 10) < StrengthAt(layer, 10, 10));
}

TEST_CASE("Full palette gives no slot")
{
	biomepaint::Layer layer = biomepaint::Make(width, height);
	for (int i = 0; i < biomepaint::maxPalette; i++) {
		REQUIRE(biomepaint::SlotOf(layer, "name" + std::to_string(i)) == i + 1);
	}
	CHECK(biomepaint::SlotOf(layer, "x256") == 0);
	CHECK(biomepaint::SlotOf(layer, "name0") == 1);
	CHECK(layer.palette.size() == static_cast<size_t>(biomepaint::maxPalette));
}

TEST_CASE("Slots map to the table by name")
{
	biomepaint::Layer layer = biomepaint::Make(width, height);
	biomepaint::SlotOf(layer, "B");
	biomepaint::SlotOf(layer, "Gone");
	biomepaint::SlotOf(layer, "A");
	std::vector<biome::Biome> table(2);
	table[0].name = "A";
	table[1].name = "B";
	const std::array<int, 256> slots = biomepaint::SlotBiomes(layer, table);
	CHECK(slots[0] == -1);
	CHECK(slots[1] == 1);
	CHECK(slots[2] == -1);
	CHECK(slots[3] == 0);
	CHECK(slots[4] == -1);
	CHECK(slots[255] == -1);
}

TEST_CASE("Painted materials mix the normalised mixes")
{
	biome::MaterialWeights automatic{};
	automatic[0] = 2.0f;
	automatic[1] = 2.0f;
	biome::Biome painted;
	painted.materials = { biome::MaterialLayer{ 2, 1.0f } };
	const biome::MaterialWeights quarter = biomepaint::PaintedMaterials(automatic, painted, 0.25f);
	CHECK(quarter[0] == doctest::Approx(0.375f));
	CHECK(quarter[1] == doctest::Approx(0.375f));
	CHECK(quarter[2] == doctest::Approx(0.25f));
	const biome::MaterialWeights none = biomepaint::PaintedMaterials(automatic, painted, 0.0f);
	CHECK(none[0] == doctest::Approx(0.5f));
	CHECK(none[2] == doctest::Approx(0.0f));
	const biome::MaterialWeights full = biomepaint::PaintedMaterials(automatic, painted, 1.0f);
	CHECK(full[0] == doctest::Approx(0.0f));
	CHECK(full[2] == doctest::Approx(1.0f));
}

TEST_CASE("Painted biome without materials keeps the automatic mix")
{
	biome::MaterialWeights automatic{};
	automatic[0] = 1.0f;
	automatic[3] = 3.0f;
	biome::Biome painted;
	for (biome::MaterialLayer& layer : painted.materials) {
		layer.weight = 0.0f;
	}
	const biome::MaterialWeights full = biomepaint::PaintedMaterials(automatic, painted, 1.0f);
	CHECK(full[0] == doctest::Approx(0.25f));
	CHECK(full[3] == doctest::Approx(0.75f));
}

TEST_CASE("BiomePaint Make rejects non-positive sizes and ignores invalid materials")
{
	const biomepaint::Layer empty = biomepaint::Make(-4, 10);
	CHECK(empty.texels.empty());

	biome::MaterialWeights automatic{};
	automatic[1] = 1.0f;
	biome::Biome painted;
	painted.materials = { biome::MaterialLayer{ 99, 1.0f } };
	const biome::MaterialWeights mixed = biomepaint::PaintedMaterials(automatic, painted, 1.0f);
	CHECK(mixed[1] == doctest::Approx(1.0f));
}
