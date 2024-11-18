/**
 * @file    SimplexNoise.cpp
 * @brief   A Perlin Simplex Noise C++ Implementation (1D, 2D, 3D).
 *
 * Copyright (c) 2014-2018 Sebastien Rombauts (sebastien.rombauts@gmail.com)
 *
 * This C++ implementation is based on the speed-improved Java version 2012-03-09
 * by Stefan Gustavson (original Java source code in the public domain).
 * http://webstaff.itn.liu.se/~stegu/simplexnoise/SimplexNoise.java:
 * - Based on example code by Stefan Gustavson (stegu@itn.liu.se).
 * - Optimisations by Peter Eastman (peastman@drizzle.stanford.edu).
 * - Better rank ordering method by Stefan Gustavson in 2012.
 *
 * This implementation is "Simplex Noise" as presented by
 * Ken Perlin at a relatively obscure and not often cited course
 * session "Real-Time Shading" at Siggraph 2001 (before real
 * time shading actually took on), under the title "hardware noise".
 * The 3D function is numerically equivalent to his Java reference
 * code available in the PDF course notes, although I re-implemented
 * it from scratch to get more readable code. The 1D, 2D and 4D cases
 * were implemented from scratch by me from Ken Perlin's text.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include "SimplexNoise.h"

#include <cstdint>  // int32_t/uint8_t
#include <algorithm>
#include <random>
#include <iterator>

 /**
  * Computes the largest integer value not greater than the float one
  *
  * This method is faster than using (int32_t)std::floor(fp).
  *
  * I measured it to be approximately twice as fast:
  *  float:  ~18.4ns instead of ~39.6ns on an AMD APU),
  *  double: ~20.6ns instead of ~36.6ns on an AMD APU),
  * Reference: http://www.codeproject.com/Tips/700780/Fast-floor-ceiling-functions
  *
  * @param[in] fp    float input value
  *
  * @return largest integer value not greater than fp
  */
static inline int32_t fastfloor(float fp) {
	int32_t i = static_cast<int32_t>(fp);
	return (fp < i) ? (i - 1) : (i);
}

/**
 * Permutation table. This is just a random jumble of all numbers 0-255.
 *
 * This produce a repeatable pattern of 256, but Ken Perlin stated
 * that it is not a problem for graphic texture as the noise features disappear
 * at a distance far enough to be able to see a repeatable pattern of 256.
 *
 * This needs to be exactly the same for all instances on all platforms,
 * so it's easiest to just keep it as static explicit data.
 * This also removes the need for any initialisation of this class.
 *
 * Note that making this an uint32_t[] instead of a uint8_t[] might make the
 * code run faster on platforms with a high penalty for unaligned single
 * byte addressing. Intel x86 is generally single-byte-friendly, but
 * some other CPUs are faster with 4-aligned reads.
 * However, a char[] is smaller, which avoids cache trashing, and that
 * is probably the most important aspect on most architectures.
 * This array is accessed a *lot* by the noise functions.
 * A vector-valued noise over 3D accesses it 96 times, and a
 * float-valued 4D noise 64 times. We want this to fit in the cache!
 */
static uint8_t perm[256] = {
	160, 151, 91, 137, 15, 90, 13, 131, 95, 201, 53, 96, 233, 194, 225, 7, 36, 140, 30, 103, 142, 69, 99, 8, 240, 37, 10, 21, 6,
	190, 148, 247, 234, 120, 0, 75, 26, 197, 252, 62, 203, 219, 35, 117, 32, 11, 57, 33, 177, 237, 88, 56, 149, 174, 87, 125, 20,
	171, 136, 68, 168, 74, 175, 71, 165, 139, 134, 27, 48, 77, 166, 158, 146, 83, 231, 229, 111, 60, 122, 133, 211, 220, 230, 92,
	105, 55, 41, 245, 46, 244, 143, 102, 65, 54, 63, 25, 1, 161, 80, 216, 209, 73, 132, 76, 208, 187, 18, 89, 200, 169, 130, 196,
	116, 135, 86, 188, 100, 164, 198, 109, 186, 173, 64, 3, 217, 52, 250, 226, 123, 124, 202, 5, 147, 38, 126, 118, 255, 82, 212,
	85, 206, 207, 227, 59, 16, 47, 17, 58, 189, 182, 42, 28, 183, 223, 213, 170, 248, 119, 2, 152, 154, 44, 70, 163, 153, 101, 221,
	155, 167, 9, 43, 22, 129, 253, 39, 108, 19, 110, 98, 113, 79, 232, 224, 185, 178, 104, 112, 246, 218, 228, 97, 34, 251, 193,
	210, 238, 12, 144, 179, 191, 241, 162, 51, 81, 235, 145, 14, 249, 107, 192, 49, 31, 214, 199, 157, 106, 84, 184, 176, 204, 121,
	115, 45, 50, 4, 127, 254, 150, 236, 93, 205, 114, 222, 29, 67, 72, 24, 141, 243, 195, 128, 66, 78, 61, 215, 180, 156
};
static uint8_t originalPerm[256] = {
	160, 151, 91, 137, 15, 90, 13, 131, 95, 201, 53, 96, 233, 194, 225, 7, 36, 140, 30, 103, 142, 69, 99, 8, 240, 37, 10, 21, 6,
	190, 148, 247, 234, 120, 0, 75, 26, 197, 252, 62, 203, 219, 35, 117, 32, 11, 57, 33, 177, 237, 88, 56, 149, 174, 87, 125, 20,
	171, 136, 68, 168, 74, 175, 71, 165, 139, 134, 27, 48, 77, 166, 158, 146, 83, 231, 229, 111, 60, 122, 133, 211, 220, 230, 92,
	105, 55, 41, 245, 46, 244, 143, 102, 65, 54, 63, 25, 1, 161, 80, 216, 209, 73, 132, 76, 208, 187, 18, 89, 200, 169, 130, 196,
	116, 135, 86, 188, 100, 164, 198, 109, 186, 173, 64, 3, 217, 52, 250, 226, 123, 124, 202, 5, 147, 38, 126, 118, 255, 82, 212,
	85, 206, 207, 227, 59, 16, 47, 17, 58, 189, 182, 42, 28, 183, 223, 213, 170, 248, 119, 2, 152, 154, 44, 70, 163, 153, 101, 221,
	155, 167, 9, 43, 22, 129, 253, 39, 108, 19, 110, 98, 113, 79, 232, 224, 185, 178, 104, 112, 246, 218, 228, 97, 34, 251, 193,
	210, 238, 12, 144, 179, 191, 241, 162, 51, 81, 235, 145, 14, 249, 107, 192, 49, 31, 214, 199, 157, 106, 84, 184, 176, 204, 121,
	115, 45, 50, 4, 127, 254, 150, 236, 93, 205, 114, 222, 29, 67, 72, 24, 141, 243, 195, 128, 66, 78, 61, 215, 180, 156
};

/**
* Shuffling the permutation table in order to have a repeatable pattern of 256 based on the seed
*
* @param[in] seed  integer value to shuffle the permutation table
*
* @note This function can be called anytime before generating noise to shuffle the permutation table
*/
void SimplexNoise::reseed(int seed) {
	std::copy(std::begin(originalPerm), std::end(originalPerm), std::begin(perm));
	
	if (seed == 0) return;

	std::mt19937 generator(seed);
	std::shuffle(std::begin(perm), std::end(perm), generator);
}

/**
 * Helper function to hash an integer using the above permutation table
 *
 *  This inline function costs around 1ns, and is called N+1 times for a noise of N dimension.
 *
 *  Using a real hash function would be better to improve the "repeatability of 256" of the above permutation table,
 * but fast integer Hash functions uses more time and have bad random properties.
 *
 * @param[in] i Integer value to hash
 *
 * @return 8-bits hashed value
 */
static inline uint8_t hash(int32_t i) {
	return perm[static_cast<uint8_t>(i)];
}

/* NOTE Gradient table to test if lookup-table are more efficient than calculs
static const float gradients1D[16] = {
		-8.f, -7.f, -6.f, -5.f, -4.f, -3.f, -2.f, -1.f,
		 1.f,  2.f,  3.f,  4.f,  5.f,  6.f,  7.f,  8.f
};
*/

/**
 * Helper function to compute gradients-dot-residual vectors (1D)
 *
 * @note that these generate gradients of more than unit length. To make
 * a close match with the value range of classic Perlin noise, the final
 * noise values need to be rescaled to fit nicely within [-1,1].
 * (The simplex noise functions as such also have different scaling.)
 * Note also that these noise functions are the most practical and useful
 * signed version of Perlin noise.
 *
 * @param[in] hash  hash value
 * @param[in] x     distance to the corner
 *
 * @return gradient value
 */
static float grad(int32_t hash, float x) {
	const int32_t h = hash & 0x0F;  // Convert low 4 bits of hash code
	float grad = 1.0f + (h & 7);    // Gradient value 1.0, 2.0, ..., 8.0
	if ((h & 8) != 0) grad = -grad; // Set a random sign for the gradient
	//  float grad = gradients1D[h];    // NOTE : Test of Gradient look-up table instead of the above
	return (grad * x);              // Multiply the gradient with the distance
}

/**
 * Helper functions to compute gradients-dot-residual vectors (2D)
 *
 * @param[in] hash  hash value
 * @param[in] x     x coord of the distance to the corner
 * @param[in] y     y coord of the distance to the corner
 *
 * @return gradient value
 */
static float grad(int32_t hash, float x, float y) {
	const int32_t h = hash & 0x3F;  // Convert low 3 bits of hash code
	const float u = h < 4 ? x : y;  // into 8 simple gradient directions,
	const float v = h < 4 ? y : x;
	return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v); // and compute the dot product with (x,y).
}

/**
 * Helper functions to compute gradients-dot-residual vectors (3D)
 *
 * @param[in] hash  hash value
 * @param[in] x     x coord of the distance to the corner
 * @param[in] y     y coord of the distance to the corner
 * @param[in] z     z coord of the distance to the corner
 *
 * @return gradient value
 */
static float grad(int32_t hash, float x, float y, float z) {
	int h = hash & 15;     // Convert low 4 bits of hash code into 12 simple
	float u = h < 8 ? x : y; // gradient directions, and compute dot product.
	float v = h < 4 ? y : h == 12 || h == 14 ? x : z; // Fix repeats at h = 12 to 15
	return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

static float grad(int32_t hash, float x, float y, float z, float t) {
	int h = hash & 31;      // Convert low 5 bits of hash code into 32 simple
	float u = h < 24 ? x : y; // gradient directions, and compute dot product.
	float v = h < 16 ? y : z;
	float w = h < 8 ? z : t;
	return ((h & 1) ? -u : u) + ((h & 2) ? -v : v) + ((h & 4) ? -w : w);
}

/**
 * 1D Perlin simplex noise
 *
 *  Takes around 74ns on an AMD APU.
 *
 * @param[in] x float coordinate
 *
 * @return Noise value in the range[-1; 1], value of 0 on all integer coordinates.
 */
float SimplexNoise::noise(float x) {
	float n0, n1;   // Noise contributions from the two "corners"

	// No need to skew the input space in 1D

	// Corners coordinates (nearest integer values):
	int32_t i0 = fastfloor(x);
	int32_t i1 = i0 + 1;
	// Distances to corners (between 0 and 1):
	float x0 = x - i0;
	float x1 = x0 - 1.0f;

	// Calculate the contribution from the first corner
	float t0 = 1.0f - x0 * x0;
	//  if(t0 < 0.0f) t0 = 0.0f; // not possible
	t0 *= t0;
	n0 = t0 * t0 * grad(hash(i0), x0);

	// Calculate the contribution from the second corner
	float t1 = 1.0f - x1 * x1;
	//  if(t1 < 0.0f) t1 = 0.0f; // not possible
	t1 *= t1;
	n1 = t1 * t1 * grad(hash(i1), x1);

	// The maximum value of this noise is 8*(3/4)^4 = 2.53125
	// A factor of 0.395 scales to fit exactly within [-1,1]
	return 0.395f * (n0 + n1);
}

/**
 * 2D Perlin simplex noise
 *
 *  Takes around 150ns on an AMD APU.
 *
 * @param[in] x float coordinate
 * @param[in] y float coordinate
 *
 * @return Noise value in the range[-1; 1], value of 0 on all integer coordinates.
 */
float SimplexNoise::noise(float x, float y) {
	float n0, n1, n2;   // Noise contributions from the three corners

	// Skewing/Unskewing factors for 2D
	static const float F2 = 0.366025403f;  // F2 = (sqrt(3) - 1) / 2
	static const float G2 = 0.211324865f;  // G2 = (3 - sqrt(3)) / 6   = F2 / (1 + 2 * K)

	// Skew the input space to determine which simplex cell we're in
	const float s = (x + y) * F2;  // Hairy factor for 2D
	const float xs = x + s;
	const float ys = y + s;
	const int32_t i = fastfloor(xs);
	const int32_t j = fastfloor(ys);

	// Unskew the cell origin back to (x,y) space
	const float t = static_cast<float>(i + j) * G2;
	const float X0 = i - t;
	const float Y0 = j - t;
	const float x0 = x - X0;  // The x,y distances from the cell origin
	const float y0 = y - Y0;

	// For the 2D case, the simplex shape is an equilateral triangle.
	// Determine which simplex we are in.
	int32_t i1, j1;  // Offsets for second (middle) corner of simplex in (i,j) coords
	if (x0 > y0) {   // lower triangle, XY order: (0,0)->(1,0)->(1,1)
		i1 = 1;
		j1 = 0;
	}
	else {   // upper triangle, YX order: (0,0)->(0,1)->(1,1)
		i1 = 0;
		j1 = 1;
	}

	// A step of (1,0) in (i,j) means a step of (1-c,-c) in (x,y), and
	// a step of (0,1) in (i,j) means a step of (-c,1-c) in (x,y), where
	// c = (3-sqrt(3))/6

	const float x1 = x0 - i1 + G2;            // Offsets for middle corner in (x,y) unskewed coords
	const float y1 = y0 - j1 + G2;
	const float x2 = x0 - 1.0f + 2.0f * G2;   // Offsets for last corner in (x,y) unskewed coords
	const float y2 = y0 - 1.0f + 2.0f * G2;

	// Work out the hashed gradient indices of the three simplex corners
	const int gi0 = hash(i + hash(j));
	const int gi1 = hash(i + i1 + hash(j + j1));
	const int gi2 = hash(i + 1 + hash(j + 1));

	// Calculate the contribution from the first corner
	float t0 = 0.5f - x0 * x0 - y0 * y0;
	if (t0 < 0.0f) {
		n0 = 0.0f;
	}
	else {
		t0 *= t0;
		n0 = t0 * t0 * grad(gi0, x0, y0);
	}

	// Calculate the contribution from the second corner
	float t1 = 0.5f - x1 * x1 - y1 * y1;
	if (t1 < 0.0f) {
		n1 = 0.0f;
	}
	else {
		t1 *= t1;
		n1 = t1 * t1 * grad(gi1, x1, y1);
	}

	// Calculate the contribution from the third corner
	float t2 = 0.5f - x2 * x2 - y2 * y2;
	if (t2 < 0.0f) {
		n2 = 0.0f;
	}
	else {
		t2 *= t2;
		n2 = t2 * t2 * grad(gi2, x2, y2);
	}

	// Add contributions from each corner to get the final noise value.
	// The result is scaled to return values in the interval [-1,1].
	return 45.23065f * (n0 + n1 + n2);
}


/**
 * 3D Perlin simplex noise
 *
 * @param[in] x float coordinate
 * @param[in] y float coordinate
 * @param[in] z float coordinate
 *
 * @return Noise value in the range[-1; 1], value of 0 on all integer coordinates.
 */
float SimplexNoise::noise(float x, float y, float z) {
	float n0, n1, n2, n3; // Noise contributions from the four corners

	// Skewing/Unskewing factors for 3D
	static const float F3 = 1.0f / 3.0f;
	static const float G3 = 1.0f / 6.0f;

	// Skew the input space to determine which simplex cell we're in
	float s = (x + y + z) * F3; // Very nice and simple skew factor for 3D
	int i = fastfloor(x + s);
	int j = fastfloor(y + s);
	int k = fastfloor(z + s);
	float t = (i + j + k) * G3;
	float X0 = i - t; // Unskew the cell origin back to (x,y,z) space
	float Y0 = j - t;
	float Z0 = k - t;
	float x0 = x - X0; // The x,y,z distances from the cell origin
	float y0 = y - Y0;
	float z0 = z - Z0;

	// For the 3D case, the simplex shape is a slightly irregular tetrahedron.
	// Determine which simplex we are in.
	int i1, j1, k1; // Offsets for second corner of simplex in (i,j,k) coords
	int i2, j2, k2; // Offsets for third corner of simplex in (i,j,k) coords
	if (x0 >= y0) {
		if (y0 >= z0) {
			i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; // X Y Z order
		}
		else if (x0 >= z0) {
			i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; // X Z Y order
		}
		else {
			i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; // Z X Y order
		}
	}
	else { // x0<y0
		if (y0 < z0) {
			i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; // Z Y X order
		}
		else if (x0 < z0) {
			i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; // Y Z X order
		}
		else {
			i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; // Y X Z order
		}
	}

	// A step of (1,0,0) in (i,j,k) means a step of (1-c,-c,-c) in (x,y,z),
	// a step of (0,1,0) in (i,j,k) means a step of (-c,1-c,-c) in (x,y,z), and
	// a step of (0,0,1) in (i,j,k) means a step of (-c,-c,1-c) in (x,y,z), where
	// c = 1/6.
	float x1 = x0 - i1 + G3; // Offsets for second corner in (x,y,z) coords
	float y1 = y0 - j1 + G3;
	float z1 = z0 - k1 + G3;
	float x2 = x0 - i2 + 2.0f * G3; // Offsets for third corner in (x,y,z) coords
	float y2 = y0 - j2 + 2.0f * G3;
	float z2 = z0 - k2 + 2.0f * G3;
	float x3 = x0 - 1.0f + 3.0f * G3; // Offsets for last corner in (x,y,z) coords
	float y3 = y0 - 1.0f + 3.0f * G3;
	float z3 = z0 - 1.0f + 3.0f * G3;

	// Work out the hashed gradient indices of the four simplex corners
	int gi0 = hash(i + hash(j + hash(k)));
	int gi1 = hash(i + i1 + hash(j + j1 + hash(k + k1)));
	int gi2 = hash(i + i2 + hash(j + j2 + hash(k + k2)));
	int gi3 = hash(i + 1 + hash(j + 1 + hash(k + 1)));

	// Calculate the contribution from the four corners
	float t0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
	if (t0 < 0) {
		n0 = 0.0;
	}
	else {
		t0 *= t0;
		n0 = t0 * t0 * grad(gi0, x0, y0, z0);
	}
	float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
	if (t1 < 0) {
		n1 = 0.0;
	}
	else {
		t1 *= t1;
		n1 = t1 * t1 * grad(gi1, x1, y1, z1);
	}
	float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
	if (t2 < 0) {
		n2 = 0.0;
	}
	else {
		t2 *= t2;
		n2 = t2 * t2 * grad(gi2, x2, y2, z2);
	}
	float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3;
	if (t3 < 0) {
		n3 = 0.0;
	}
	else {
		t3 *= t3;
		n3 = t3 * t3 * grad(gi3, x3, y3, z3);
	}
	// Add contributions from each corner to get the final noise value.
	// The result is scaled to stay just inside [-1,1]
	return 32.0f * (n0 + n1 + n2 + n3);
}

/**
* 4D Perlin simplex noise
 *
 * @param[in] x float coordinate
 * @param[in] y float coordinate
 * @param[in] z float coordinate
 * @param[in] w float coordinate
 *
 * @return Noise value in the range[-1; 1], value of 0 on all integer coordinates.
 */
float SimplexNoise::noise(float x, float y, float z, float w) {
	float n0, n1, n2, n3, n4; // Noise contributions from the five corners

	// Skewing/Unskewing factors for 4D
	static const float F4 = (sqrt(5.0f) - 1.0f) / 4.0f;
	static const float G4 = (5.0f - sqrt(5.0f)) / 20.0f;

	// Skew the (x,y,z,w) space to determine which cell of 24 simplices we're in
	float s = (x + y + z + w) * F4; // Factor for 4D skewing
	int i = fastfloor(x + s);
	int j = fastfloor(y + s);
	int k = fastfloor(z + s);
	int l = fastfloor(w + s);
	float t = (i + j + k + l) * G4; // Factor for 4D unskewing
	float X0 = i - t; // Unskew the cell origin back to (x,y,z,w) space
	float Y0 = j - t;
	float Z0 = k - t;
	float W0 = l - t;
	float x0 = x - X0;  // The x,y,z,w distances from the cell origin
	float y0 = y - Y0;
	float z0 = z - Z0;
	float w0 = w - W0;

	// For the 4D case, the simplex is a 4D shape I won't even try to describe.
	// To find out which of the 24 possible simplices we're in, we need to determine the magnitude ordering of x0, y0, z0 and w0.
	// Six pair-wise comparisons are performed between each possible pair of the four coordinates, and the results are used to rank the coordinates.
	int rankx = 0;
	int ranky = 0;
	int rankz = 0;
	int rankw = 0;
	if (x0 > y0) rankx++; else ranky++;
	if (x0 > z0) rankx++; else rankz++;
	if (x0 > w0) rankx++; else rankw++;
	if (y0 > z0) ranky++; else rankz++;
	if (y0 > w0) ranky++; else rankw++;
	if (z0 > w0) rankz++; else rankw++;
	int i1, j1, k1, l1; // The integer offsets for the second simplex corner
	int i2, j2, k2, l2; // The integer offsets for the third simplex corner
	int i3, j3, k3, l3; // The integer offsets for the fourth simplex corner
	// simplex[c] is a 4-vector with the numbers 0, 1, 2 and 3 in some order.
	// Many values of c will never occur, since e.g. x>y>z>w makes x<z, y<w and x<w impossible.
	// Only the 24 indices which have non-zero entries make any sense.
	// We use a thresholding to set the coordinates in turn from the largest magnitude.
	// Rank 3 denotes the largest coordinate.
	i1 = rankx >= 3 ? 1 : 0;
	j1 = ranky >= 3 ? 1 : 0;
	k1 = rankz >= 3 ? 1 : 0;
	l1 = rankw >= 3 ? 1 : 0;
	// Rank 2 denotes the second largest coordinate.
	i2 = rankx >= 2 ? 1 : 0;
	j2 = ranky >= 2 ? 1 : 0;
	k2 = rankz >= 2 ? 1 : 0;
	l2 = rankw >= 2 ? 1 : 0;
	// Rank 1 denotes the second smallest coordinate.
	i3 = rankx >= 1 ? 1 : 0;
	j3 = ranky >= 1 ? 1 : 0;
	k3 = rankz >= 1 ? 1 : 0;
	l3 = rankw >= 1 ? 1 : 0;
	// The fifth corner has all coordinate offsets = 1, so no need to compute that.
	float x1 = x0 - i1 + G4; // Offsets for second corner in (x,y,z,w) coordinates
	float y1 = y0 - j1 + G4;
	float z1 = z0 - k1 + G4;
	float w1 = w0 - l1 + G4;
	float x2 = x0 - i2 + 2.0f * G4; // Offsets for third corner in (x,y,z,w) coordinates
	float y2 = y0 - j2 + 2.0f * G4;
	float z2 = z0 - k2 + 2.0f * G4;
	float w2 = w0 - l2 + 2.0f * G4;
	float x3 = x0 - i3 + 3.0f * G4; // Offsets for fourth corner in (x,y,z,w) coordinates
	float y3 = y0 - j3 + 3.0f * G4;
	float z3 = z0 - k3 + 3.0f * G4;
	float w3 = w0 - l3 + 3.0f * G4;
	float x4 = x0 - 1.0f + 4.0f * G4; // Offsets for last corner in (x,y,z,w) coordinates
	float y4 = y0 - 1.0f + 4.0f * G4;
	float z4 = z0 - 1.0f + 4.0f * G4;
	float w4 = w0 - 1.0f + 4.0f * G4;
	// Work out the hashed gradient indices of the five simplex corners
	int gi0 = hash(i + hash(j + hash(k + hash(l))));
	int gi1 = hash(i + i1 + hash(j + j1 + hash(k + k1 + hash(l + l1))));
	int gi2 = hash(i + i2 + hash(j + j2 + hash(k + k2 + hash(l + l2))));
	int gi3 = hash(i + i3 + hash(j + j3 + hash(k + k3 + hash(l + l3))));
	int gi4 = hash(i + 1 + hash(j + 1 + hash(k + 1 + hash(l + 1))));
	// Calculate the contribution from the five corners
	float t0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0 - w0 * w0;
	if (t0 < 0) n0 = 0.0;
	else {
		t0 *= t0;
		n0 = t0 * t0 * grad(gi0, x0, y0, z0, w0);
	}
	float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1 - w1 * w1;
	if (t1 < 0) n1 = 0.0;
	else {
		t1 *= t1;
		n1 = t1 * t1 * grad(gi1, x1, y1, z1, w1);
	}
	float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2 - w2 * w2;
	if (t2 < 0) n2 = 0.0;
	else {
		t2 *= t2;
		n2 = t2 * t2 * grad(gi2, x2, y2, z2, w2);
	}
	float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3 - w3 * w3;
	if (t3 < 0) n3 = 0.0;
	else {
		t3 *= t3;
		n3 = t3 * t3 * grad(gi3, x3, y3, z3, w3);
	}
	float t4 = 0.6f - x4 * x4 - y4 * y4 - z4 * z4 - w4 * w4;
	if (t4 < 0) n4 = 0.0;
	else {
		t4 *= t4;
		n4 = t4 * t4 * grad(gi4, x4, y4, z4, w4);
	}
	// Sum up and scale the result to cover the range [-1,1]
	return 27.0f * (n0 + n1 + n2 + n3 + n4);
}


/**
 * Fractal/Fractional Brownian Motion (fBm) summation of 1D Perlin Simplex noise
 *
 * @param[in] octaves   number of fraction of noise to sum
 * @param[in] x         float coordinate
 *
 * @return Noise value in the range[-1; 1], value of 0 on all integer coordinates.
 */
float SimplexNoise::fractal(size_t octaves, float x) const {
	float output = 0.f;
	float denom = 0.f;
	float frequency = mFrequency;
	float amplitude = mAmplitude;

	for (size_t i = 0; i < octaves; i++) {
		output += (amplitude * noise(x * frequency));
		denom += amplitude;

		frequency *= mLacunarity;
		amplitude *= mPersistence;
	}

	return (output / denom);
}

/**
 * Fractal/Fractional Brownian Motion (fBm) summation of 2D Perlin Simplex noise
 *
 * @param[in] octaves   number of fraction of noise to sum
 * @param[in] x         x float coordinate
 * @param[in] y         y float coordinate
 *
 * @return Noise value in the range[-1; 1], value of 0 on all integer coordinates.
 */
float SimplexNoise::fractal(size_t octaves, float x, float y) const {
	float output = 0.f;
	float denom = 0.f;
	float frequency = mFrequency;
	float amplitude = mAmplitude;

	for (size_t i = 0; i < octaves; i++) {
		output += (amplitude * noise(x * frequency, y * frequency));
		denom += amplitude;

		frequency *= mLacunarity;
		amplitude *= mPersistence;
	}

	return (output / denom);
}

/**
 * Fractal/Fractional Brownian Motion (fBm) summation of 3D Perlin Simplex noise
 *
 * @param[in] octaves   number of fraction of noise to sum
 * @param[in] x         x float coordinate
 * @param[in] y         y float coordinate
 * @param[in] z         z float coordinate
 *
 * @return Noise value in the range[-1; 1], value of 0 on all integer coordinates.
 */
float SimplexNoise::fractal(size_t octaves, float x, float y, float z) const {
	float output = 0.f;
	float denom = 0.f;
	float frequency = mFrequency;
	float amplitude = mAmplitude;

	for (size_t i = 0; i < octaves; i++) {
		output += (amplitude * noise(x * frequency, y * frequency, z * frequency));
		denom += amplitude;

		frequency *= mLacunarity;
		amplitude *= mPersistence;
	}

	return (output / denom);
}
