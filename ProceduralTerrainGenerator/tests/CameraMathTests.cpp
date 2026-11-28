#include "doctest/doctest.h"

#include <algorithm>
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

namespace {
	//World point (XZ) shown at ndc of a frame: +X to the right, -Z up
	glm::vec2 FlatPoint(const cameramath::FlatFrame& f, glm::vec2 ndc)
	{
		return { f.center.x + ndc.x * f.half.x, f.center.y - ndc.y * f.half.y };
	}

	//A finite map 1024 x 512 world units at (100, -50) shown in a square view
	cameramath::FlatZoom MapZoom(cameramath::FlatFrame& fit)
	{
		cameramath::FlatZoom zoom;
		cameramath::SetFlatFit(zoom, glm::vec2(100.0f, -50.0f) - glm::vec2(512.0f, 256.0f), glm::vec2(100.0f, -50.0f) + glm::vec2(512.0f, 256.0f));
		fit = cameramath::FitFlatFrame(zoom.fitMin, zoom.fitMax, 1.0f);
		return zoom;
	}
}

TEST_CASE("Flat view: the fitted frame has a margin and the aspect of the view")
{
	const cameramath::FlatFrame fit = cameramath::FitFlatFrame(glm::vec2(-100.0f, -50.0f), glm::vec2(100.0f, 50.0f), 1.0f);
	CHECK(fit.center.x == doctest::Approx(0.0f));
	CHECK(fit.half.x == doctest::Approx(102.0f));
	CHECK(fit.half.y == doctest::Approx(102.0f));
	const cameramath::FlatFrame wide = cameramath::FitFlatFrame(glm::vec2(-100.0f, -50.0f), glm::vec2(100.0f, 50.0f), 4.0f);
	CHECK(wide.half.x == doctest::Approx(204.0f));
	CHECK(wide.half.y == doctest::Approx(51.0f));
	//Not zoomed = the fitted frame
	cameramath::FlatZoom zoom;
	const cameramath::FlatFrame shown = cameramath::Zoomed(fit, zoom);
	CHECK(shown.half.x == doctest::Approx(fit.half.x));
	CHECK(shown.center.y == doctest::Approx(fit.center.y));
}

TEST_CASE("Flat view: zooming keeps the point under the cursor")
{
	cameramath::FlatFrame fit;
	cameramath::FlatZoom zoom = MapZoom(fit);
	const glm::vec2 cursor(0.4f, -0.7f);
	for (float factor : { 0.5f, 0.8f, 0.5f, 1.25f }) {
		const glm::vec2 before = FlatPoint(cameramath::Zoomed(fit, zoom), cursor);
		cameramath::ZoomFlatAt(zoom, fit, cursor, factor);
		const glm::vec2 after = FlatPoint(cameramath::Zoomed(fit, zoom), cursor);
		CHECK(after.x == doctest::Approx(before.x).epsilon(1e-4));
		CHECK(after.y == doctest::Approx(before.y).epsilon(1e-4));
	}
	CHECK(zoom.scale == doctest::Approx(0.5f * 0.8f * 0.5f * 1.25f));
}

TEST_CASE("Flat view: zoom limits on a finite map")
{
	cameramath::FlatFrame fit;
	cameramath::FlatZoom zoom = MapZoom(fit);
	for (int i = 0; i < 100; i++) {
		cameramath::ZoomFlatAt(zoom, fit, glm::vec2(0.0f), 0.5f);
	}
	//32 texels (4 world units each) across the shorter side
	CHECK(std::min(cameramath::Zoomed(fit, zoom).half.x, cameramath::Zoomed(fit, zoom).half.y) == doctest::Approx(64.0f));
	for (int i = 0; i < 100; i++) {
		cameramath::ZoomFlatAt(zoom, fit, glm::vec2(0.9f, 0.9f), 2.0f);
	}
	CHECK(zoom.scale == doctest::Approx(1.25f));
	//The centre never leaves the fitted frame
	CHECK(std::abs(zoom.offset.x) <= fit.half.x + 1e-3f);
	CHECK(std::abs(zoom.offset.y) <= fit.half.y + 1e-3f);
}

TEST_CASE("Flat view: panning moves the map with the cursor and stops at the edge of the frame")
{
	cameramath::FlatFrame fit;
	cameramath::FlatZoom zoom = MapZoom(fit);
	cameramath::ZoomFlatAt(zoom, fit, glm::vec2(0.0f), 0.25f);
	//The point under the cursor moves with it: 100 px right and 50 px down in a view 800 px high
	const cameramath::FlatFrame before = cameramath::Zoomed(fit, zoom);
	const glm::vec2 grabbed = FlatPoint(before, glm::vec2(0.0f));
	cameramath::PanFlat(zoom, fit, glm::vec2(100.0f, 50.0f), 800.0f);
	const glm::vec2 ndcMoved(100.0f / 400.0f, -50.0f / 400.0f);
	const glm::vec2 now = FlatPoint(cameramath::Zoomed(fit, zoom), ndcMoved);
	CHECK(now.x == doctest::Approx(grabbed.x).epsilon(1e-4));
	CHECK(now.y == doctest::Approx(grabbed.y).epsilon(1e-4));
	cameramath::PanFlat(zoom, fit, glm::vec2(-1e6f, 1e6f), 800.0f);
	CHECK(zoom.offset.x == doctest::Approx(fit.half.x));
	CHECK(zoom.offset.y == doctest::Approx(-fit.half.y));
}

TEST_CASE("Flat view: an infinite world zooms only, inside the area it shows")
{
	cameramath::FlatFrame fit;
	cameramath::FlatZoom zoom = MapZoom(fit);
	zoom.fixedFrame = true;
	cameramath::ZoomFlatAt(zoom, fit, glm::vec2(0.0f), 2.0f);
	CHECK(zoom.scale == doctest::Approx(1.0f));
	cameramath::ZoomFlatAt(zoom, fit, glm::vec2(1.0f, 1.0f), 0.5f);
	const cameramath::FlatFrame shown = cameramath::Zoomed(fit, zoom);
	CHECK(shown.center.x + shown.half.x <= fit.center.x + fit.half.x + 1e-3f);
	CHECK(shown.center.y - shown.half.y >= fit.center.y - fit.half.y - 1e-3f);
	const glm::vec2 offset = zoom.offset;
	cameramath::PanFlat(zoom, fit, glm::vec2(300.0f, 0.0f), 800.0f);
	CHECK(zoom.offset.x == offset.x);
	//Zooming out again ends at the whole area
	cameramath::ZoomFlatAt(zoom, fit, glm::vec2(-1.0f, 0.3f), 4.0f);
	CHECK(zoom.offset.x == doctest::Approx(0.0f));
	CHECK(zoom.offset.y == doctest::Approx(0.0f));
}

TEST_CASE("Flat view: a frame of another size resets the zoom, a moved frame keeps it")
{
	cameramath::FlatFrame fit;
	cameramath::FlatZoom zoom = MapZoom(fit);
	cameramath::ZoomFlatAt(zoom, fit, glm::vec2(0.5f, 0.5f), 0.5f);
	cameramath::SetFlatFit(zoom, zoom.fitMin + glm::vec2(40.0f), zoom.fitMax + glm::vec2(40.0f));
	CHECK(zoom.scale == doctest::Approx(0.5f));
	//The area of the infinite world follows the camera: centre +- radius, whose float difference wobbles by rounding
	cameramath::SetFlatFit(zoom, glm::vec2(-4097.3f), glm::vec2(4097.3f));
	cameramath::ZoomFlatAt(zoom, cameramath::FitFlatFrame(zoom.fitMin, zoom.fitMax, 1.0f), glm::vec2(0.0f), 0.5f);
	for (float c = 0.0f; c < 50000.0f; c += 1234.567f) {
		cameramath::SetFlatFit(zoom, glm::vec2(c, -c) - glm::vec2(4097.3f), glm::vec2(c, -c) + glm::vec2(4097.3f));
	}
	CHECK(zoom.scale == doctest::Approx(0.5f));
	cameramath::SetFlatFit(zoom, glm::vec2(100.0f, -50.0f) - glm::vec2(512.0f, 256.0f), glm::vec2(100.0f, -50.0f) + glm::vec2(512.0f, 256.0f));
	cameramath::ZoomFlatAt(zoom, fit, glm::vec2(0.5f, 0.5f), 0.5f);
	cameramath::SetFlatFit(zoom, zoom.fitMin, zoom.fitMax + glm::vec2(1100.0f, 0.0f));
	CHECK(zoom.scale == 1.0f);
	CHECK(zoom.offset == glm::vec2(0.0f));
	//Reset (F, "Fit 2D map")
	cameramath::ZoomFlatAt(zoom, fit, glm::vec2(0.5f, 0.5f), 0.5f);
	cameramath::ResetFlatZoom(zoom);
	CHECK(zoom.scale == 1.0f);
	CHECK(zoom.offset == glm::vec2(0.0f));
}
