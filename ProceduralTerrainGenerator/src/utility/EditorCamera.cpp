#include "EditorCamera.h"

#include <algorithm>
#include <cmath>

#include "glm/gtc/matrix_transform.hpp"
#include "imgui.h"

#include "CameraMath.h"

namespace {
	//Framebuffer pixels per window point (cursor positions are in window points)
	glm::dvec2 FramebufferScale(GLFWwindow* window)
	{
		int windowWidth = 0, windowHeight = 0, framebufferWidth = 0, framebufferHeight = 0;
		glfwGetWindowSize(window, &windowWidth, &windowHeight);
		glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
		if (windowWidth <= 0 || windowHeight <= 0) {
			return glm::dvec2(1.0);
		}
		return glm::dvec2(framebufferWidth, framebufferHeight) / glm::dvec2(windowWidth, windowHeight);
	}

	//Cursor in framebuffer pixels, origin at the bottom left (as glViewport and glReadPixels)
	glm::vec2 ToFramebuffer(GLFWwindow* window, glm::dvec2 cursor)
	{
		int framebufferWidth = 0, framebufferHeight = 0;
		glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
		const glm::dvec2 scaled = cursor * FramebufferScale(window);
		return glm::vec2(static_cast<float>(scaled.x), static_cast<float>(framebufferHeight - scaled.y));
	}

	bool Inside(glm::vec2 pixel, glm::ivec4 viewport)
	{
		return pixel.x >= viewport.x && pixel.y >= viewport.y && pixel.x < viewport.x + viewport.z && pixel.y < viewport.y + viewport.w;
	}

	cameramath::Pose PoseOf(const Camera& camera)
	{
		return { camera.GetPosition(), camera.GetYaw(), camera.GetPitch() };
	}

	void SetPose(Camera& camera, const cameramath::Pose& pose)
	{
		camera.SetView(pose.position, pose.yaw, pose.pitch);
	}

	//Frame of the 2D map before zooming, as TerrainView fits it into the view
	cameramath::FlatFrame FlatFit(const cameramath::FlatZoom& flat, glm::ivec4 viewport)
	{
		return cameramath::FitFlatFrame(flat.fitMin, flat.fitMax, viewport.w > 0 ? static_cast<float>(viewport.z) / viewport.w : 1.0f);
	}

	//Distance of the pivot when nothing is under the cursor (sky): two seconds of flight, at least a few units
	float FallbackDistance(const Camera& camera)
	{
		return std::max(2.0f * camera.GetSpeed(), 10.0f);
	}
}

void EditorCamera::Update(GLFWwindow* window, Camera& camera, glm::ivec4 viewport, cameramath::FlatZoom* flat)
{
	const bool down[3] = {
		glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS,
		glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS,
		glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS,
	};
	bool pressed[3];
	for (int i = 0; i < 3; i++) {
		pressed[i] = down[i] && !wasDown[i];
		wasDown[i] = down[i];
	}
	glm::dvec2 cursor;
	glfwGetCursorPos(window, &cursor.x, &cursor.y);

	//A 3D drag ends when the 2D map comes, and the other way round
	if (!enabled || camera.MouseControl() || (drag != Drag::NONE && (drag == Drag::FLAT_PAN) != (flat != nullptr))) {
		End(window, camera);
		if (!enabled || camera.MouseControl()) {
			return;
		}
	}
	if (flat) {
		UpdateFlat(window, down, pressed, cursor, viewport, *flat);
		return;
	}
	if (drag == Drag::NONE) {
		//ImGui of the last frame: the cursor is not over a panel
		const bool overScene = !ImGui::GetIO().WantCaptureMouse && Inside(ToFramebuffer(window, cursor), viewport);
		if (!overScene) {
			return;
		}
		const bool alt = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
		if (pressed[1]) {
			Begin(window, camera, Drag::LOOK, cursor);
		}
		else if (pressed[2]) {
			Begin(window, camera, Drag::PAN, cursor);
		}
		else if (pressed[0] && alt) {
			Begin(window, camera, Drag::ORBIT, cursor);
		}
		return;
	}

	const bool held = drag == Drag::LOOK ? down[1] : drag == Drag::PAN ? down[2] : down[0];
	if (!held) {
		End(window, camera);
		return;
	}
	const glm::vec2 delta(cursor - lastCursor);
	lastCursor = cursor;
	if (delta == glm::vec2(0.0f)) {
		return;
	}
	const float sensitivity = camera.GetSensitivity();
	//Mouse right turns right, mouse up (y down in window coordinates) looks up, as with M
	switch (drag) {
	case Drag::LOOK:
		SetPose(camera, cameramath::Look(PoseOf(camera), delta.x * sensitivity, -delta.y * sensitivity));
		break;
	case Drag::ORBIT:
		SetPose(camera, cameramath::Orbit(PoseOf(camera), pivot, delta.x * sensitivity, -delta.y * sensitivity));
		break;
	case Drag::PAN: {
		const float unitsPerPixel = cameramath::UnitsPerPixel(pivotDistance, camera.GetFov(), viewport.w);
		const glm::vec2 pixels = delta * glm::vec2(FramebufferScale(window));
		camera.SetPosition(cameramath::Pan(PoseOf(camera), pixels, unitsPerPixel));
		break;
	}
	case Drag::NONE:
		break;
	}
}

void EditorCamera::UpdateFlat(GLFWwindow* window, const bool down[3], const bool pressed[3], glm::dvec2 cursor, glm::ivec4 viewport,
	cameramath::FlatZoom& flat)
{
	//F: the whole map again (not while typing into a field)
	const bool fitKey = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
	if (fitKey && !fitKeyWasDown && !ImGui::GetIO().WantCaptureKeyboard) {
		cameramath::ResetFlatZoom(flat);
	}
	fitKeyWasDown = fitKey;
	if (drag == Drag::NONE) {
		const bool overScene = !ImGui::GetIO().WantCaptureMouse && Inside(ToFramebuffer(window, cursor), viewport);
		if (overScene && (pressed[1] || pressed[2])) {
			drag = Drag::FLAT_PAN;
			flatButton = pressed[2] ? 2 : 1;
			lastCursor = cursor;
		}
		return;
	}
	if (!down[flatButton]) {
		drag = Drag::NONE;
		return;
	}
	const glm::vec2 delta(cursor - lastCursor);
	lastCursor = cursor;
	if (delta != glm::vec2(0.0f)) {
		cameramath::PanFlat(flat, FlatFit(flat, viewport), delta * glm::vec2(FramebufferScale(window)), static_cast<float>(viewport.w));
	}
}

void EditorCamera::Begin(GLFWwindow* window, const Camera& camera, Drag kind, glm::dvec2 cursor)
{
	drag = kind;
	pressCursor = cursor;
	//Until the depth is read: a point straight ahead
	pivotDistance = FallbackDistance(camera);
	pivot = camera.GetPosition() + camera.GetFront() * pivotDistance;
	pickPending = kind != Drag::LOOK;
	//Turning hides the cursor, so it can move without reaching the edge of the screen; panning keeps it under the grabbed point
	if (kind != Drag::PAN) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}
	glfwGetCursorPos(window, &lastCursor.x, &lastCursor.y);
}

void EditorCamera::End(GLFWwindow* window, const Camera& camera)
{
	if (drag == Drag::NONE) {
		return;
	}
	//M took the mouse meanwhile: its hidden cursor stays; panning never hid it
	if (drag != Drag::PAN && drag != Drag::FLAT_PAN && !camera.MouseControl()) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		glfwSetCursorPos(window, pressCursor.x, pressCursor.y);
	}
	drag = Drag::NONE;
	pickPending = false;
}

void EditorCamera::AfterDraw(const Camera& camera, glm::ivec4 viewport)
{
	if (!pickPending) {
		return;
	}
	pickPending = false;
	GLFWwindow* window = glfwGetCurrentContext();
	const glm::vec2 pixel = glm::floor(ToFramebuffer(window, pressCursor));
	if (!Inside(pixel, viewport)) {
		return;
	}
	float depth = 1.0f;
	glReadPixels(static_cast<int>(pixel.x), static_cast<int>(pixel.y), 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
	if (!(depth < 1.0f)) {
		return;
	}
	const glm::vec3 world = glm::unProject(glm::vec3(pixel + 0.5f, depth), glm::mat4(1.0f), camera.GetViewProjection(), glm::vec4(viewport));
	const float distance = glm::distance(world, camera.GetPosition());
	if (distance > 1e-3f) {
		pivot = world;
		pivotDistance = distance;
	}
}

void EditorCamera::ImGuiWheel(GLFWwindow* window, Camera& camera, glm::ivec4 viewport, cameramath::FlatZoom* flat)
{
	const ImGuiIO& io = ImGui::GetIO();
	if (!enabled || camera.MouseControl() || io.WantCaptureMouse || io.KeyShift || io.MouseWheel == 0.0f) {
		return;
	}
	glm::dvec2 cursor;
	glfwGetCursorPos(window, &cursor.x, &cursor.y);
	const glm::vec2 pixel = ToFramebuffer(window, cursor);
	if (!Inside(pixel, viewport)) {
		return;
	}
	if (flat) {
		//A notch = 15% closer, about the point under the cursor
		const glm::vec2 ndc = (pixel - glm::vec2(viewport.x, viewport.y)) / glm::vec2(viewport.z, viewport.w) * 2.0f - 1.0f;
		cameramath::ZoomFlatAt(*flat, FlatFit(*flat, viewport), ndc, std::pow(0.85f, io.MouseWheel));
		return;
	}
	//A notch = a quarter of a second of flight
	camera.SetPosition(camera.GetPosition() + camera.GetFront() * (io.MouseWheel * 0.25f * camera.GetSpeed()));
}

void EditorCamera::ImGuiDraw()
{
	ImGui::Checkbox("Editor camera (mouse)", &enabled);
	ImGui::SetItemTooltip("Steering with the mouse over the 3D view, like in game engine editors");
	if (enabled) {
		ImGui::TextDisabled("Right button: look (+ WASD fly), Alt + left: orbit,\nmiddle: pan, wheel: forward / back");
		ImGui::TextDisabled("2D map: wheel: zoom, middle / right: drag,\nF: whole map (infinite world: zoom only)");
	}
}
