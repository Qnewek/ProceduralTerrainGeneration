#pragma once

#include <cstddef>
#include <vector>

//2D map stored row-major: element (x, y) lives at index y * width + x.
//Sizes are always passed as (width, height).
template<typename T>
class Grid
{
public:
	Grid() = default;
	Grid(int _width, int _height) { Resize(_width, _height); }

	//Reallocates the map and fills it with T{}
	//@param _width - number of columns (x)
	//@param _height - number of rows (y)
	void Resize(int _width, int _height)
	{
		width = _width;
		height = _height;
		data.assign(static_cast<size_t>(width) * height, T{});
	}

	int GetWidth() const { return width; }
	int GetHeight() const { return height; }
	size_t Size() const { return data.size(); }
	bool Empty() const { return data.empty(); }
	explicit operator bool() const { return !data.empty(); }

	T& operator[](size_t i) { return data[i]; }
	const T& operator[](size_t i) const { return data[i]; }
	T& At(int x, int y) { return data[static_cast<size_t>(y) * width + x]; }
	const T& At(int x, int y) const { return data[static_cast<size_t>(y) * width + x]; }

	//Raw pointer for OpenGL uploads; nullptr when empty
	T* Data() { return data.empty() ? nullptr : data.data(); }
	const T* Data() const { return data.empty() ? nullptr : data.data(); }

private:
	int width = 0, height = 0;
	std::vector<T> data;
};

using HeightField = Grid<float>;
using BiomeMap = Grid<int>;
