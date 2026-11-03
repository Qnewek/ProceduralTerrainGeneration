#pragma once

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

//Average and maximum of the most recent samples
class RollingStats
{
public:
	//@param _capacity - number of samples kept, must be greater than 0
	explicit RollingStats(size_t _capacity) : capacity(_capacity) { samples.reserve(_capacity); }

	//Adds a sample, replacing the oldest one when full
	void Add(double value)
	{
		if (samples.size() < capacity) {
			samples.push_back(value);
		}
		else {
			samples[next] = value;
		}
		next = (next + 1) % capacity;
	}

	//@return 0 when there are no samples
	double Average() const
	{
		return samples.empty() ? 0.0 : std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
	}

	//@return 0 when there are no samples
	double Max() const
	{
		return samples.empty() ? 0.0 : *std::max_element(samples.begin(), samples.end());
	}

private:
	size_t capacity;
	size_t next = 0;
	std::vector<double> samples;
};
