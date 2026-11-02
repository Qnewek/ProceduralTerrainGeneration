#include "doctest/doctest.h"

#include "Grid.h"

TEST_CASE("Grid stores elements row by row")
{
	Grid<int> grid(4, 3);
	CHECK(grid.GetWidth() == 4);
	CHECK(grid.GetHeight() == 3);
	CHECK(grid.Size() == 12);

	grid.At(3, 1) = 7;
	CHECK(grid[1 * 4 + 3] == 7);
}

TEST_CASE("Grid::Resize reallocates and clears the map")
{
	Grid<int> grid(2, 2);
	grid.At(1, 1) = 5;

	grid.Resize(3, 2);
	CHECK(grid.Size() == 6);
	CHECK(grid.At(1, 1) == 0);
}

TEST_CASE("An empty Grid is false and has no data pointer")
{
	Grid<float> grid;
	CHECK_FALSE(grid);
	CHECK(grid.Empty());
	CHECK(grid.Data() == nullptr);

	grid.Resize(1, 1);
	CHECK(static_cast<bool>(grid));
	CHECK(grid.Data() != nullptr);
}
