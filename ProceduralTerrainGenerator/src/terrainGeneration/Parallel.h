#pragma once

#include <algorithm>
#include <execution>
#include <numeric>
#include <vector>

//Calls fn(i) for every i in [0, count) in parallel on the standard library thread pool.
//Iterations must be independent: each one may write only its own part of the output (e.g. one map row).
//@param fn - callable taking int
template<typename F>
void ParallelFor(int count, F&& fn)
{
	std::vector<int> indices(static_cast<size_t>(std::max(count, 0)));
	std::iota(indices.begin(), indices.end(), 0);
	std::for_each(std::execution::par, indices.begin(), indices.end(), fn);
}
