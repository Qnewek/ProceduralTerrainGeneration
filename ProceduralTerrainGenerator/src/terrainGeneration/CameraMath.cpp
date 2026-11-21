#include "CameraMath.h"

#include <algorithm>
#include <cmath>

#include "glm/gtc/matrix_transform.hpp"

namespace cameramath {
	glm::vec3 Front(float yaw, float pitch)
	{
		//The ROTATE formula of Camera::UpdateCameraVectors
		const float y = glm::radians(yaw), p = glm::radians(pitch);
		return glm::normalize(glm::vec3(std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)));
	}

	glm::vec3 Right(float yaw, float pitch)
	{
		//Also at +-90 degrees of pitch: the yaw alone gives the side
		(void)pitch;
		return glm::normalize(glm::cross(Front(yaw, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
	}

	glm::vec3 Up(float yaw, float pitch)
	{
		return glm::cross(Right(yaw, pitch), Front(yaw, pitch));
	}

	Pose Look(Pose pose, float yawDelta, float pitchDelta)
	{
		pose.yaw += yawDelta;
		pose.pitch = std::clamp(pose.pitch + pitchDelta, -maxPitch, maxPitch);
		return pose;
	}

	Pose Orbit(const Pose& pose, glm::vec3 pivot, float yawDelta, float pitchDelta)
	{
		const Pose turned = Look(pose, yawDelta, pitchDelta);
		//The pitch change after the limit, so the position turns by exactly as much as the view
		const float pitchTurn = turned.pitch - pose.pitch;
		//Pitch around the right of the camera (positive tilts the view up), then yaw around the vertical (yaw grows from +X towards +Z,
		//which is a negative rotation around +Y)
		const glm::mat4 pitch = glm::rotate(glm::mat4(1.0f), glm::radians(pitchTurn), Right(pose.yaw, pose.pitch));
		const glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), glm::radians(-yawDelta), glm::vec3(0.0f, 1.0f, 0.0f));
		Pose result = turned;
		result.position = pivot + glm::vec3(yaw * pitch * glm::vec4(pose.position - pivot, 0.0f));
		return result;
	}

	glm::vec3 Pan(const Pose& pose, glm::vec2 pixels, float unitsPerPixel)
	{
		return pose.position - Right(pose.yaw, pose.pitch) * (pixels.x * unitsPerPixel) + Up(pose.yaw, pose.pitch) * (pixels.y * unitsPerPixel);
	}

	float UnitsPerPixel(float distance, float fovDegrees, int viewportHeight)
	{
		return 2.0f * distance * std::tan(glm::radians(fovDegrees) * 0.5f) / static_cast<float>(std::max(viewportHeight, 1));
	}
}
