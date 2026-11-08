#include "doctest/doctest.h"

#include <cstdint>

#include "Simplex/SimplexNoise.h"

//The permutation tables are built on the CPU and sent to the compute shaders (GpuHeightGenerator::SetNoise)

TEST_CASE("makePermutation shuffles every byte value exactly once")
{
	uint8_t perm[256];
	SimplexNoise::makePermutation(623, perm);

	int counts[256] = {};
	for (uint8_t value : perm)
		counts[value]++;

	int wrong = 0;
	for (int count : counts) {
		if (count != 1)
			wrong++;
	}
	CHECK(wrong == 0);
}

//The tables must not depend on the compiler or standard library. Expected values come from a separate implementation
//of mt19937 and Fisher-Yates, not from this code, so changing the table or the shuffle changes every generated world
TEST_CASE("makePermutation gives a fixed table for a seed")
{
	struct Expected { int seed; uint8_t first[4]; uint8_t last[4]; };
	const Expected cases[] = {
		{ 623, { 128, 6, 8, 43 }, { 145, 228, 67, 99 } },
		{ 1, { 115, 82, 72, 108 }, { 213, 41, 113, 197 } },
		{ 90210, { 162, 177, 37, 227 }, { 7, 55, 213, 85 } },
	};

	for (const Expected& expected : cases) {
		CAPTURE(expected.seed);
		uint8_t perm[256];
		SimplexNoise::makePermutation(expected.seed, perm);
		for (int i = 0; i < 4; i++) {
			CHECK(perm[i] == expected.first[i]);
			CHECK(perm[252 + i] == expected.last[i]);
		}
	}
}
