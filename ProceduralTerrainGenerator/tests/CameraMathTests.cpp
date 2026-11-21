#include "doctest/doctest.h"

#include <cmath>

#include "CameraMath.h"

namespace {
	void CheckVec(glm::vec3 a, glm::vec3 b, float epsilon = 1e-4f)
	{
		CHECK(std::abs(a.x - b.x) < epsilon);
		CHECK(std::abs(a.y - b.y) < epsilon);
		CHECK(std::abs(a.z - b.z) < epsilon);
	}

	//Direction from the camera to a point, in the basis of the camera (right, up, front)
	glm::vec3 InView(const cameramath::Pose& pose, glm::vec3 point)
	{
		const glm::vec3 d = glm::normalize(point - pose.position);
		return { glm::dot(d, cameramath::Right(pose.yaw, pose.pitch)), glm::dot(d, cameramath::Up(pose.yaw, pose.pitch)),
			glm::dot(d, cameramath::Front(pose.yaw, pose.pitch)) };
	}
}

TEST_CASE("Front follows the angles of Camera")
{
	CheckVec(cameramath::Front(0.0f, 0.0f), { 1.0f, 0.0f, 0.0f });
	CheckVec(cameramath::Front(90.0f, 0.0f), { 0.0f, 0.0f, 1.0f });
	CheckVec(cameramath::Front(0.0f, 90.0f), { 0.0f, 1.0f, 0.0f });
	//Right and up of a level camera looking along +X
	CheckVec(cameramath::Right(0.0f, 0.0f), { 0.0f, 0.0f, 1.0f });
	CheckVec(cameramath::Up(0.0f, 0.0f), { 0.0f, 1.0f, 0.0f });
}

TEST_CASE("Look turns the camera without moving it and limits the pitch")
{
	const cameramath::Pose pose{ { 10.0f, 20.0f, 30.0f }, 45.0f, 80.0f };
	const cameramath::Pose turned = cameramath::Look(pose, 30.0f, 25.0f);
	CheckVec(turned.position, pose.position);
	CHECK(turned.yaw == doctest::Approx(75.0f));
	CHECK(turned.pitch == doctest::Approx(cameramath::maxPitch));
	CHECK(cameramath::Look(pose, 0.0f, -300.0f).pitch == doctest::Approx(-cameramath::maxPitch));
}

TEST_CASE("Orbit keeps the distance to the pivot and the pivot in the same place of the view")
{
	const cameramath::Pose pose{ { 120.0f, 300.0f, -40.0f }, 30.0f, -35.0f };
	//Not in the middle of the view: the point under the cursor
	const glm::vec3 pivot = pose.position + 250.0f * glm::normalize(cameramath::Front(pose.yaw, pose.pitch) + glm::vec3(0.1f, -0.05f, 0.2f));
	for (const glm::vec2 delta : { glm::vec2(25.0f, 0.0f), glm::vec2(0.0f, 20.0f), glm::vec2(-70.0f, -30.0f) }) {
		const cameramath::Pose orbited = cameramath::Orbit(pose, pivot, delta.x, delta.y);
		CHECK(glm::distance(orbited.position, pivot) == doctest::Approx(glm::distance(pose.position, pivot)).epsilon(1e-4));
		CheckVec(InView(orbited, pivot), InView(pose, pivot));
		CHECK(orbited.yaw == doctest::Approx(pose.yaw + delta.x));
		CHECK(orbited.pitch == doctest::Approx(pose.pitch + delta.y));
	}
	//Only yaw: around the vertical through the pivot, the height stays
	const cameramath::Pose around = cameramath::Orbit(pose, pivot, 90.0f, 0.0f);
	CHECK(around.position.y == doctest::Approx(pose.position.y));
}

TEST_CASE("Orbit limits the pitch and still keeps the pivot in place")
{
	const cameramath::Pose pose{ { 0.0f, 100.0f, 0.0f }, 0.0f, -80.0f };
	const glm::vec3 pivot(20.0f, 0.0f, 5.0f);
	const cameramath::Pose orbited = cameramath::Orbit(pose, pivot, 0.0f, -30.0f);
	CHECK(orbited.pitch == doctest::Approx(-cameramath::maxPitch));
	CHECK(glm::distance(orbited.position, pivot) == doctest::Approx(glm::distance(pose.position, pivot)).epsilon(1e-4));
	CheckVec(InView(orbited, pivot), InView(pose, pivot));
}

TEST_CASE("Pan moves the camera in the plane of the screen")
{
	const cameramath::Pose pose{ { 5.0f, 50.0f, 5.0f }, 120.0f, -40.0f };
	const glm::vec3 moved = cameramath::Pan(pose, { 30.0f, -40.0f }, 0.5f);
	const glm::vec3 step = moved - pose.position;
	CHECK(std::abs(glm::dot(step, cameramath::Front(pose.yaw, pose.pitch))) < 1e-4f);
	CHECK(glm::length(step) == doctest::Approx(25.0f));
	//Dragging right moves the camera left (the terrain follows the cursor), dragging up moves it down
	CHECK(glm::dot(step, cameramath::Right(pose.yaw, pose.pitch)) == doctest::Approx(-15.0f));
	CHECK(glm::dot(step, cameramath::Up(pose.yaw, pose.pitch)) == doctest::Approx(-20.0f));
}

TEST_CASE("Units per pixel at a distance")
{
	CHECK(cameramath::UnitsPerPixel(100.0f, 90.0f, 200) == doctest::Approx(1.0f));
	CHECK(cameramath::UnitsPerPixel(100.0f, 90.0f, 0) > 0.0f);
}
