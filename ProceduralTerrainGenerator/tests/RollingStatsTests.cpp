#include "doctest/doctest.h"

#include "RollingStats.h"

TEST_CASE("RollingStats without samples returns zero")
{
	RollingStats stats(3);
	CHECK(stats.Average() == 0.0);
	CHECK(stats.Max() == 0.0);
}

TEST_CASE("RollingStats averages samples before it is full")
{
	RollingStats stats(4);
	stats.Add(1.0);
	stats.Add(3.0);
	CHECK(stats.Average() == 2.0);
	CHECK(stats.Max() == 3.0);
}

TEST_CASE("RollingStats forgets the oldest sample when full")
{
	RollingStats stats(3);
	stats.Add(9.0);
	stats.Add(1.0);
	stats.Add(2.0);
	stats.Add(3.0);
	CHECK(stats.Average() == 2.0);
	CHECK(stats.Max() == 3.0);

	stats.Add(4.0);
	stats.Add(5.0);
	CHECK(stats.Average() == 4.0);
	CHECK(stats.Max() == 5.0);
}
