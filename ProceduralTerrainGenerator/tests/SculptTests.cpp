#include "doctest/doctest.h"

#include <cmath>

#include "glm/gtc/matrix_transform.hpp"

#include "Sculpt.h"

namespace {
	//Not square, so swapped axes are detected
	constexpr int width = 40, height = 24;

	HeightField Flat(float value)
	{
		HeightField map(width, height);
		for (size_t i = 0; i < map.Size(); i++) {
			map[i] = value;
		}
		return map;
	}

	sculpt::Dab DabAt(glm::vec2 center, float amount, float target = 0.0f, uint32_t seed = 1)
	{
		return { center, amount, target, seed, 1.5f };
	}

	sculpt::BrushConfig Config(sculpt::Brush brush, float radius, float hardness = 0.5f)
	{
		sculpt::BrushConfig config;
		config.brush = brush;
		config.radius = radius;
		config.hardness = hardness;
		return config;
	}
}

TEST_CASE("Falloff is 1 at the centre and 0 at the radius")
{
	CHECK(sculpt::Falloff(0.0f, 10.0f, 0.5f) == doctest::Approx(1.0f));
	CHECK(sculpt::Falloff(10.0f, 10.0f, 0.5f) == doctest::Approx(0.0f));
	CHECK(sculpt::Falloff(12.0f, 10.0f, 0.5f) == 0.0f);
	const float middle = sculpt::Falloff(7.5f, 10.0f, 0.5f);
	CHECK(middle > 0.0f);
	CHECK(middle < 1.0f);
	//Full strength up to hardness * radius
	CHECK(sculpt::Falloff(4.9f, 10.0f, 0.5f) == doctest::Approx(1.0f));
}

TEST_CASE("Hard brush keeps full strength to the edge")
{
	CHECK(sculpt::Falloff(9.9f, 10.0f, 1.0f) == doctest::Approx(1.0f));
	CHECK(sculpt::Falloff(10.1f, 10.0f, 1.0f) == 0.0f);
}

TEST_CASE("Raise adds amount at the centre and nothing outside the radius")
{
	HeightField map = Flat(0.2f);
	sculpt::Apply(map, Config(sculpt::Brush::RAISE, 6.0f), DabAt({ 20.0f, 12.0f }, 0.1f));
	CHECK(map.At(20, 12) == doctest::Approx(0.3f));
	CHECK(map.At(20, 5) == doctest::Approx(0.2f));
	CHECK(map.At(27, 12) == doctest::Approx(0.2f));
	CHECK(map.At(23, 12) > 0.2f);
}

TEST_CASE("Lower takes it away")
{
	HeightField map = Flat(0.2f);
	sculpt::Apply(map, Config(sculpt::Brush::LOWER, 6.0f), DabAt({ 20.0f, 12.0f }, 0.1f));
	CHECK(map.At(20, 12) == doctest::Approx(0.1f));
	CHECK(map.At(0, 0) == doctest::Approx(0.2f));
}

TEST_CASE("Flatten moves towards the target")
{
	HeightField map = Flat(0.6f);
	sculpt::Apply(map, Config(sculpt::Brush::FLATTEN, 6.0f), DabAt({ 20.0f, 12.0f }, 1.0f, 0.2f));
	CHECK(map.At(20, 12) == doctest::Approx(0.2f));
	HeightField half = Flat(0.6f);
	sculpt::Apply(half, Config(sculpt::Brush::FLATTEN, 6.0f), DabAt({ 20.0f, 12.0f }, 0.5f, 0.2f));
	CHECK(half.At(20, 12) == doctest::Approx(0.4f));
}

TEST_CASE("Smooth flattens a spike and keeps a flat map")
{
	HeightField flat = Flat(0.3f);
	sculpt::Apply(flat, Config(sculpt::Brush::SMOOTH, 8.0f), DabAt({ 20.0f, 12.0f }, 1.0f));
	for (size_t i = 0; i < flat.Size(); i++) {
		CHECK(flat[i] == doctest::Approx(0.3f));
	}
	HeightField spike = Flat(0.0f);
	spike.At(20, 12) = 1.0f;
	sculpt::Apply(spike, Config(sculpt::Brush::SMOOTH, 8.0f), DabAt({ 20.0f, 12.0f }, 1.0f));
	CHECK(spike.At(20, 12) < 0.5f);
	CHECK(spike.At(21, 12) > 0.0f);
}

TEST_CASE("Noise is deterministic and bounded")
{
	float difference = 0.0f;
	for (int i = 0; i < 200; i++) {
		const glm::vec2 p(0.37f * i, 1.91f * i - 40.0f);
		const float a = sculpt::NoiseAt(p, 16.0f, 7);
		CHECK(a == sculpt::NoiseAt(p, 16.0f, 7));
		CHECK(std::abs(a) <= 1.0f);
		difference += std::abs(a - sculpt::NoiseAt(p, 16.0f, 8));
	}
	CHECK(difference > 1.0f);
}

TEST_CASE("Brush near the border touches only map texels")
{
	const sculpt::Dab dab = DabAt({ -3.0f, 5.0f }, 0.1f);
	const glm::ivec4 area = sculpt::Area(dab, 8.0f, { width, height });
	CHECK(area.x == 0);
	CHECK(area.y >= 0);
	//Up to the texel at the radius (weight 0 there)
	CHECK(area.x + area.z <= 6);
	CHECK(area.y + area.w <= height);
	HeightField map = Flat(0.0f);
	sculpt::Apply(map, Config(sculpt::Brush::RAISE, 8.0f), dab);
	CHECK(map.At(0, 5) > 0.0f);
	for (int y = 0; y < height; y++) {
		for (int x = 5; x < width; x++) {
			CHECK(map.At(x, y) == 0.0f);
		}
	}
	//Wholly outside
	CHECK(sculpt::Area(DabAt({ -30.0f, 5.0f }, 0.1f), 8.0f, { width, height }).z == 0);
}

TEST_CASE("Stroke limit leaves room for a long raise on a flat map")
{
	//A flat map has 1 m units: a 1.5 unit cap would stop a raise at 1.5 m
	const float limit = sculpt::StrokeLimit(1.0f, 0.0f, 10.0f);
	CHECK(limit >= 10.0f * sculpt::strokeHeadroomSeconds - 1e-3f);
	//Never below the usual bound
	CHECK(sculpt::StrokeLimit(1000.0f, 800.0f, 10.0f) == 1.5f);
	//Room above the highest point, not above 0, in map units
	CHECK(100.0f * sculpt::StrokeLimit(100.0f, 400.0f, 10.0f) >= 400.0f + 10.0f * sculpt::strokeHeadroomSeconds - 1e-2f);
}

TEST_CASE("Heights are clamped to the limit")
{
	HeightField map = Flat(1.4f);
	sculpt::Apply(map, Config(sculpt::Brush::RAISE, 6.0f), DabAt({ 20.0f, 12.0f }, 0.5f));
	CHECK(map.At(20, 12) == doctest::Approx(1.5f));
}

TEST_CASE("Pick and TexelToWorld round trip in 3D and 2D")
{
	const glm::ivec2 size(width, height);
	const glm::ivec4 viewport(100, 50, 800, 600);
	const float modelScale = 2.0f, heightScale = 64.0f;
	const glm::mat4 perspective = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 5000.0f)
		* glm::lookAt(glm::vec3(30.0f, 200.0f, 260.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	//Top-down like the 2D map: +X right, -Z up
	const glm::mat4 flat = glm::ortho(-200.0f, 200.0f, -150.0f, 150.0f, 0.0f, 400.0f)
		* glm::lookAt(glm::vec3(0.0f, 200.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
	for (const glm::mat4& viewProjection : { perspective, flat }) {
		for (const glm::vec2 texel : { glm::vec2(3.0f, 4.0f), glm::vec2(20.5f, 12.0f), glm::vec2(39.0f, 23.0f) }) {
			const glm::vec3 world = sculpt::TexelToWorld(texel, 0.3f, heightScale, modelScale, size);
			const glm::vec3 window = glm::project(world, glm::mat4(1.0f), viewProjection, glm::vec4(viewport));
			const std::optional<glm::vec2> picked = sculpt::PickTexel(glm::vec2(window), window.z, viewport, viewProjection, modelScale, size);
			REQUIRE(picked.has_value());
			//Window depth in float loses some precision far from the near plane; a small part of a texel is enough for a brush
			CHECK(std::abs(picked->x - texel.x) < 0.05f);
			CHECK(std::abs(picked->y - texel.y) < 0.05f);
		}
		CHECK_FALSE(sculpt::PickTexel({ 400.0f, 300.0f }, 1.0f, viewport, viewProjection, modelScale, size).has_value());
		//Off the map
		const glm::vec3 outside = sculpt::TexelToWorld({ 60.0f, 12.0f }, 0.0f, heightScale, modelScale, size);
		const glm::vec3 window = glm::project(outside, glm::mat4(1.0f), viewProjection, glm::vec4(viewport));
		CHECK_FALSE(sculpt::PickTexel(glm::vec2(window), window.z, viewport, viewProjection, modelScale, size).has_value());
	}
}
