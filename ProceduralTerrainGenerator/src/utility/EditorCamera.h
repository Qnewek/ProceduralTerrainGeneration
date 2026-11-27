#pragma once

#include "glm/glm.hpp"

#include "Camera.h"
#include "CameraMath.h"

//Mouse steering of the camera like in game engine editors, over the 3D view and when M (mouse look) is off:
//right button held = look around (with WASD/Space/Left Shift = fly), Alt + left button = orbit around the point under the cursor,
//middle button = pan in the plane of the screen, wheel = forward / back along the view (Shift + wheel is left to the brushes).
//The point under the cursor comes from the depth of the drawn scene. The math is in cameramath (CameraMath.h).
//On the 2D map: wheel = zoom about the cursor, middle or right button = drag the map, F = the whole map again (the infinite world only zooms).
class EditorCamera
{
public:
	//Before Camera::SteerCamera: starts, continues or ends a drag
	//@param viewport - x, y, width, height of the 3D view in framebuffer pixels, origin at the bottom left
	//@param flat - zoom of the 2D map when it is shown (the camera is then left alone), nullptr = 3D view
	void Update(GLFWwindow* window, Camera& camera, glm::ivec4 viewport, cameramath::FlatZoom* flat);
	//After the scene is drawn, before ImGui: the point under the cursor for a drag started this frame
	void AfterDraw(const Camera& camera, glm::ivec4 viewport);
	//Inside the ImGui frame (the wheel of the frame): moves the camera along the view, or zooms the 2D map
	void ImGuiWheel(GLFWwindow* window, Camera& camera, glm::ivec4 viewport, cameramath::FlatZoom* flat);
	//Checkbox and help for the Camera tab
	void ImGuiDraw();

private:
	enum class Drag { NONE, LOOK, ORBIT, PAN, FLAT_PAN };
	Drag drag = Drag::NONE;
	bool enabled = true;
	//The pivot and its distance are read from the depth after the scene of this frame is drawn
	bool pickPending = false;
	glm::vec3 pivot = glm::vec3(0.0f);
	float pivotDistance = 1.0f;
	glm::dvec2 lastCursor = glm::dvec2(0.0), pressCursor = glm::dvec2(0.0);
	//Left, right, middle button in the last Update; the button holding a 2D drag
	bool wasDown[3] = { false, false, false };
	int flatButton = 2;
	bool fitKeyWasDown = false;

	void Begin(GLFWwindow* window, const Camera& camera, Drag kind, glm::dvec2 cursor);
	void End(GLFWwindow* window, const Camera& camera);
	void UpdateFlat(GLFWwindow* window, const bool down[3], const bool pressed[3], glm::dvec2 cursor, glm::ivec4 viewport, cameramath::FlatZoom& flat);
};
