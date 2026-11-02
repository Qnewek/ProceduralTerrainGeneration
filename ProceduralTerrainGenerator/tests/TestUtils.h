#pragma once

//Non-square on purpose - generation has to keep (width, height) and y * width + x apart
constexpr int TEST_WIDTH = 64;
constexpr int TEST_HEIGHT = 48;

//Counts the cells for which pred(x, y) is true. Lets a test make one assertion per map instead of one per cell
template<typename F>
int CountCells(int width, int height, F pred)
{
	int count = 0;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (pred(x, y))
				count++;
		}
	}
	return count;
}
