#pragma once

#include <cstdint>
#include <iterator>
#include <map>
#include <optional>

//Ranges of a buffer of `capacity` elements handed out and given back (first fit); free neighbours merge again.
//Growing keeps every range where it is, so the owner only has to copy the old contents to the bigger buffer.
class RangeAllocator
{
public:
	explicit RangeAllocator(uint32_t _capacity = 0) { Grow(_capacity); }

	uint32_t Capacity() const { return capacity; }
	uint32_t Used() const { return used; }

	//@return the first element of a free range of count elements, none if no free range is long enough
	std::optional<uint32_t> Allocate(uint32_t count)
	{
		if (count == 0) {
			return 0u;
		}
		for (auto it = free.begin(); it != free.end(); ++it) {
			if (it->second < count) {
				continue;
			}
			const uint32_t offset = it->first, length = it->second;
			free.erase(it);
			if (length > count) {
				free.emplace(offset + count, length - count);
			}
			used += count;
			return offset;
		}
		return std::nullopt;
	}

	//Gives back a range returned by Allocate
	void Free(uint32_t offset, uint32_t count)
	{
		if (count == 0) {
			return;
		}
		used -= count;
		auto it = free.emplace(offset, count).first;
		//Merge with the next and the previous free range
		auto next = std::next(it);
		if (next != free.end() && it->first + it->second == next->first) {
			it->second += next->second;
			free.erase(next);
		}
		if (it != free.begin()) {
			auto previous = std::prev(it);
			if (previous->first + previous->second == it->first) {
				previous->second += it->second;
				free.erase(it);
			}
		}
	}

	//Adds elements at the end; ranges handed out stay valid
	void Grow(uint32_t newCapacity)
	{
		if (newCapacity <= capacity) {
			return;
		}
		const uint32_t added = newCapacity - capacity;
		const uint32_t start = capacity;
		capacity = newCapacity;
		Free(start, added);
		used += added;	//Free counted the new elements as given back
	}

private:
	std::map<uint32_t, uint32_t> free;	//first element -> length
	uint32_t capacity = 0;
	uint32_t used = 0;
};
