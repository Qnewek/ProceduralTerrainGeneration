#include "doctest/doctest.h"

#include "glm/gtc/matrix_transform.hpp"

#include "Frustum.h"

namespace {
	//Camera at the origin looking along -Z, 90 degrees field of view, square screen, view from 1 to 100
	Frustum TestFrustum()
	{
		const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
		const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		return Frustum(projection * view);
	}

	bool Contains(const Frustum& frustum, glm::vec3 center, float half)
	{
		return frustum.Intersects(center - half, center + half);
	}
}

TEST_CASE("Frustum keeps boxes in view and rejects boxes behind, beside and beyond it")
{
	const Frustum frustum = TestFrustum();
	CHECK(Contains(frustum, { 0.0f, 0.0f, -10.0f }, 1.0f));
	CHECK_FALSE(Contains(frustum, { 0.0f, 0.0f, 10.0f }, 1.0f));		//behind
	CHECK_FALSE(Contains(frustum, { 30.0f, 0.0f, -10.0f }, 1.0f));	//right of the 45 degree edge
	CHECK_FALSE(Contains(frustum, { 0.0f, -30.0f, -10.0f }, 1.0f));	//below
	CHECK_FALSE(Contains(frustum, { 0.0f, 0.0f, -200.0f }, 1.0f));	//past the far plane
	CHECK_FALSE(Contains(frustum, { 0.0f, 0.0f, -0.2f }, 0.1f));		//before the near plane
}

TEST_CASE("Frustum keeps a box that only crosses a plane")
{
	const Frustum frustum = TestFrustum();
	CHECK(Contains(frustum, { 10.5f, 0.0f, -10.0f }, 1.0f));		//edge at x = 10 cuts it
	CHECK(Contains(frustum, { 0.0f, 0.0f, 0.0f }, 2.0f));			//camera inside the box
	CHECK(Contains(frustum, { 0.0f, 0.0f, -100.5f }, 1.0f));		//far plane cuts it
}

TEST_CASE("Frustum of projection * view * model works in model space")
{
	const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
	const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(4.0f));
	const Frustum frustum(projection * view * model);
	CHECK(Contains(frustum, { 0.0f, 0.0f, -10.0f }, 1.0f));			//world z = -40
	CHECK_FALSE(Contains(frustum, { 0.0f, 0.0f, -30.0f }, 1.0f));	//world z = -120, past the far plane
}
