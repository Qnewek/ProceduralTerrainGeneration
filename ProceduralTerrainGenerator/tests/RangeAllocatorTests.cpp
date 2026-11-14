#include "doctest/doctest.h"

#include "RangeAllocator.h"

TEST_CASE("RangeAllocator: first fit, freeing merges neighbours, growing keeps ranges")
{
	RangeAllocator a(100);
	const auto first = a.Allocate(40);
	const auto second = a.Allocate(40);
	REQUIRE(first);
	REQUIRE(second);
	CHECK(*first == 0);
	CHECK(*second == 40);
	CHECK(a.Used() == 80);
	CHECK(!a.Allocate(30));

	a.Free(*first, 40);
	CHECK(a.Used() == 40);
	//The hole at the start is used first
	const auto third = a.Allocate(10);
	REQUIRE(third);
	CHECK(*third == 0);

	a.Free(*third, 10);
	a.Free(*second, 40);
	CHECK(a.Used() == 0);
	//All merged back into one range
	const auto whole = a.Allocate(100);
	REQUIRE(whole);
	CHECK(*whole == 0);

	a.Grow(150);
	CHECK(a.Capacity() == 150);
	CHECK(a.Used() == 100);
	const auto added = a.Allocate(50);
	REQUIRE(added);
	CHECK(*added == 100);
	CHECK(!a.Allocate(1));
}

TEST_CASE("RangeAllocator: a free range at the end merges with grown space")
{
	RangeAllocator a(10);
	const auto r = a.Allocate(5);
	REQUIRE(r);
	a.Grow(20);
	//5 free at the end of the old capacity + 10 new = 15 in one range
	const auto big = a.Allocate(15);
	REQUIRE(big);
	CHECK(*big == 5);
	CHECK(a.Allocate(0) == 0u);
}
