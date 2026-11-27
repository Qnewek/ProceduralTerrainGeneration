#pragma once

#include "glm/glm.hpp"

//Mouse steering of the camera like in game engine editors (EditorCamera): look around, orbit around a point, pan in the plane of the screen.
//Angles in degrees with the conventions of Camera (yaw 0 = +X, 90 = +Z; pitch up positive), world up = +Y
namespace cameramath {
	struct Pose {
		glm::vec3 position = glm::vec3(0.0f);
		float yaw = 0.0f;
		float pitch = 0.0f;
	};

	//Pitch limit of Camera
	inline constexpr float maxPitch = 89.0f;

	glm::vec3 Front(float yaw, float pitch);
	//normalize(cross(front, world up)), as Camera moves sideways
	glm::vec3 Right(float yaw, float pitch);
	//Up of the view: cross(right, front)
	glm::vec3 Up(float yaw, float pitch);

	//Turns the camera in place; pitch limited to +-maxPitch
	Pose Look(Pose pose, float yawDelta, float pitchDelta);
	//Turns the camera and carries it around pivot by the same rotation (yaw around the vertical through the pivot, pitch around the right
	//of the camera), so the distance to the pivot and the place of the pivot in the view stay; the pitch change is cut to the limit
	Pose Orbit(const Pose& pose, glm::vec3 pivot, float yawDelta, float pitchDelta);
	//@param pixels - movement of the cursor, x right, y down (window coordinates)
	//@param unitsPerPixel - world units one pixel covers at the grabbed point (UnitsPerPixel)
	//@return position moved so that the grabbed point follows the cursor
	glm::vec3 Pan(const Pose& pose, glm::vec2 pixels, float unitsPerPixel);
	//World units one pixel of a perspective view covers at the distance; viewportHeight in pixels (at least 1)
	float UnitsPerPixel(float distance, float fovDegrees, int viewportHeight);

	//2D map (top-down orthographic view, +X to the right, -Z up): XZ in world units before modelScale
	struct FlatFrame {
		glm::vec2 center = glm::vec2(0.0f);
		glm::vec2 half = glm::vec2(1.0f);		//half sides along X and Z
	};
	//Zoom of the 2D map relative to the frame the mode fits (the whole map, maps side by side, the area of the infinite world)
	struct FlatZoom {
		float scale = 1.0f;						//half size of the view over that of the fitted frame
		glm::vec2 offset = glm::vec2(0.0f);		//centre of the view from the centre of the fitted frame
		bool fixedFrame = false;				//infinite world: zoom only, the view stays inside the fitted frame
		float minHalf = 64.0f;					//smallest half of the shorter side of the view: 32 texels of 4 world units across
		//Area (min, max) the mode fitted last; another size resets the zoom (SetFlatFit)
		glm::vec2 fitMin = glm::vec2(0.0f), fitMax = glm::vec2(0.0f);
	};
	//Largest zoom out of a finite map, over the fitted frame
	inline constexpr float flatMaxScale = 1.25f;

	//Frame of min - max with a 2% margin, widened to the aspect (width / height) of the view
	FlatFrame FitFlatFrame(glm::vec2 min, glm::vec2 max, float aspect);
	//The frame shown: the fitted one zoomed
	FlatFrame Zoomed(const FlatFrame& fit, const FlatZoom& zoom);
	//Records the area the mode fits; an area of another size resets the zoom, a moved one keeps it
	void SetFlatFit(FlatZoom& zoom, glm::vec2 min, glm::vec2 max);
	void ResetFlatZoom(FlatZoom& zoom);
	//Keeps the scale in its limits and the view where it may be (centre inside the fitted frame; with fixedFrame the whole view)
	void ClampFlatZoom(FlatZoom& zoom, const FlatFrame& fit);
	//Zooms by factor (< 1 = closer) keeping the point at ndc (-1 to 1 over the view, y up) in place
	void ZoomFlatAt(FlatZoom& zoom, const FlatFrame& fit, glm::vec2 ndc, float factor);
	//Moves the view so the map follows the cursor (nothing with fixedFrame)
	//@param pixels - movement of the cursor, x right, y down; viewportHeight - height of the view in the same pixels
	void PanFlat(FlatZoom& zoom, const FlatFrame& fit, glm::vec2 pixels, float viewportHeight);
}
