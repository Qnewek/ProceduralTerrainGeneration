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
}
