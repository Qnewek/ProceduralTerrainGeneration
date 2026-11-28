#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <random>

namespace noise {
	//Fixed base permutation table of 0..255 used to derive per-seed tables for GPU simplex noise shaders
	inline constexpr std::array<uint8_t, 256> basePermutation = {
		160, 151, 91, 137, 15, 90, 13, 131, 95, 201, 53, 96, 233, 194, 225, 7, 36, 140, 30, 103, 142, 69, 99, 8, 240, 37, 10, 21, 6,
		190, 148, 247, 234, 120, 0, 75, 26, 197, 252, 62, 203, 219, 35, 117, 32, 11, 57, 33, 177, 237, 88, 56, 149, 174, 87, 125, 20,
		171, 136, 68, 168, 74, 175, 71, 165, 139, 134, 27, 48, 77, 166, 158, 146, 83, 231, 229, 111, 60, 122, 133, 211, 220, 230, 92,
		105, 55, 41, 245, 46, 244, 143, 102, 65, 54, 63, 25, 1, 161, 80, 216, 209, 73, 132, 76, 208, 187, 18, 89, 200, 169, 130, 196,
		116, 135, 86, 188, 100, 164, 198, 109, 186, 173, 64, 3, 217, 52, 250, 226, 123, 124, 202, 5, 147, 38, 126, 118, 255, 82, 212,
		85, 206, 207, 227, 59, 16, 47, 17, 58, 189, 182, 42, 28, 183, 223, 213, 170, 248, 119, 2, 152, 154, 44, 70, 163, 153, 101, 221,
		155, 167, 9, 43, 22, 129, 253, 39, 108, 19, 110, 98, 113, 79, 232, 224, 185, 178, 104, 112, 246, 218, 228, 97, 34, 251, 193,
		210, 238, 12, 144, 179, 191, 241, 162, 51, 81, 235, 145, 14, 249, 107, 192, 49, 31, 214, 199, 157, 106, 84, 184, 176, 204, 121,
		115, 45, 50, 4, 127, 254, 150, 236, 93, 205, 114, 222, 29, 67, 72, 24, 141, 243, 195, 128, 66, 78, 61, 215, 180, 156,
		23, 40, 94, 138, 159, 172, 181, 239, 242
	};

	//Builds a deterministic 256-entry permutation table for the given seed (independent of std::shuffle implementation)
	inline void MakePermutation(int seed, uint8_t out[256])
	{
		std::copy(std::begin(basePermutation), std::end(basePermutation), out);
		std::mt19937 generator(static_cast<uint32_t>(seed));
		for (int i = 255; i > 0; i--) {
			std::swap(out[i], out[generator() % static_cast<uint32_t>(i + 1)]);
		}
	}
}
