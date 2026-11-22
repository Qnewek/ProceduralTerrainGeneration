#pragma once

#include "glm/glm.hpp"

#include "Camera.h"

//Mouse steering of the camera like in game engine editors, over the 3D view and when M (mouse look) is off:
//right button held = look around (with WASD/Space/Left Shift = fly), Alt + left button = orbit around the point under the cursor,
//middle button = pan in the plane of the screen, wheel = forward / back along the view (Shift + wheel is left to the brushes).
//The point under the cursor comes from the depth of the drawn scene. The math is in cameramath (CameraMath.h).
class EditorCamera
{
public:
	//Before Camera::SteerCamera: starts, continues or ends a drag
	//@param viewport - x, y, width, height of the 3D view in framebuffer pixels, origin at the bottom left
	//@param flatView - the 2D map is shown: nothing to steer (its frame is set by the modes)
	void Update(GLFWwindow* window, Camera& camera, glm::ivec4 viewport, bool flatView);
	//After the scene is drawn, before ImGui: the point under the cursor for a drag started this frame
	void AfterDraw(const Camera& camera, glm::ivec4 viewport);
	//Inside the ImGui frame (the wheel of the frame): moves the camera along the view
	void ImGuiWheel(GLFWwindow* window, Camera& camera, glm::ivec4 viewport, bool flatView);
	//Checkbox and help for the Camera tab
	void ImGuiDraw();

private:
	enum class Drag { NONE, LOOK, ORBIT, PAN };
	Drag drag = Drag::NONE;
	bool enabled = true;
	//The pivot and its distance are read from the depth after the scene of this frame is drawn
	bool pickPending = false;
	glm::vec3 pivot = glm::vec3(0.0f);
	float pivotDistance = 1.0f;
	glm::dvec2 lastCursor = glm::dvec2(0.0), pressCursor = glm::dvec2(0.0);
	//Left, right, middle button in the last Update
	bool wasDown[3] = { false, false, false };

	void Begin(GLFWwindow* window, const Camera& camera, Drag kind, glm::dvec2 cursor);
	void End(GLFWwindow* window, const Camera& camera);
};
