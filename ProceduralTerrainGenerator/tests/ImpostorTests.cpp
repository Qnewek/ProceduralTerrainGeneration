#include "doctest/doctest.h"

#include <cmath>

#include "Impostor.h"

using namespace impostor;

TEST_CASE("Hemi-octahedral mapping round-trips directions of the upper hemisphere")
{
	for (int j = 0; j <= 16; j++) {
		for (int i = 0; i <= 16; i++) {
			const glm::vec2 uv(i / 16.0f, j / 16.0f);
			const glm::vec3 d = HemiOctDecode(uv);
			CHECK(glm::length(d) == doctest::Approx(1.0f).epsilon(1e-5));
			CHECK(d.y >= -1e-6f);
			const glm::vec2 back = HemiOctEncode(d);
			CHECK(back.x == doctest::Approx(uv.x).epsilon(1e-4));
			CHECK(back.y == doctest::Approx(uv.y).epsilon(1e-4));
		}
	}
}

TEST_CASE("Hemi-octahedral mapping puts straight up in the middle and the horizon on the border")
{
	const glm::vec2 up = HemiOctEncode(glm::vec3(0.0f, 1.0f, 0.0f));
	CHECK(up.x == doctest::Approx(0.5f));
	CHECK(up.y == doctest::Approx(0.5f));
	for (float angle = 0.0f; angle < 6.28f; angle += 0.3f) {
		const glm::vec2 uv = HemiOctEncode(glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
		//The mapping is turned by 45 degrees, so the horizon lies on the border of the square
		const float border = std::max(std::abs(uv.x - 0.5f), std::abs(uv.y - 0.5f));
		CHECK(border == doctest::Approx(0.5f).epsilon(1e-4));
	}
}

TEST_CASE("Impostor frames look from the upper hemisphere with an orthonormal basis")
{
	constexpr int frames = 8;
	for (int j = 0; j < frames; j++) {
		for (int i = 0; i < frames; i++) {
			const glm::vec3 d = FrameDirection(i, j, frames);
			CHECK(d.y >= -1e-6f);
			glm::vec3 right, up;
			FrameAxes(d, right, up);
			CHECK(glm::length(right) == doctest::Approx(1.0f).epsilon(1e-5));
			CHECK(glm::length(up) == doctest::Approx(1.0f).epsilon(1e-5));
			CHECK(glm::dot(right, d) == doctest::Approx(0.0f).epsilon(1e-5));
			CHECK(glm::dot(up, d) == doctest::Approx(0.0f).epsilon(1e-5));
			CHECK(glm::dot(right, up) == doctest::Approx(0.0f).epsilon(1e-5));
			//Upright: the frame's up never points down, so trees are not drawn upside down
			CHECK(up.y >= -1e-6f);
		}
	}
	//Straight up has no horizontal direction; the basis must still be valid
	glm::vec3 right, up;
	FrameAxes(glm::vec3(0.0f, 1.0f, 0.0f), right, up);
	CHECK(glm::length(right) == doctest::Approx(1.0f));
	CHECK(glm::length(up) == doctest::Approx(1.0f));
}
