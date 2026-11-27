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

	FlatFrame FitFlatFrame(glm::vec2 min, glm::vec2 max, float aspect)
	{
		FlatFrame frame;
		frame.center = (min + max) * 0.5f;
		frame.half = glm::max((max - min) * 0.5f * 1.02f, glm::vec2(1e-3f));
		aspect = aspect > 0.0f ? aspect : 1.0f;
		if (frame.half.x < frame.half.y * aspect) {
			frame.half.x = frame.half.y * aspect;
		}
		else {
			frame.half.y = frame.half.x / aspect;
		}
		return frame;
	}

	FlatFrame Zoomed(const FlatFrame& fit, const FlatZoom& zoom)
	{
		return { fit.center + zoom.offset, fit.half * zoom.scale };
	}

	void SetFlatFit(FlatZoom& zoom, glm::vec2 min, glm::vec2 max)
	{
		//An area moving with the camera has its size wobble by rounding (centre +- radius), so only a real change counts
		const glm::vec2 size = max - min, last = zoom.fitMax - zoom.fitMin;
		const glm::vec2 tolerance = 1e-4f * glm::max(glm::abs(size), glm::vec2(1.0f));
		if (glm::any(glm::greaterThan(glm::abs(size - last), tolerance))) {
			ResetFlatZoom(zoom);
		}
		zoom.fitMin = min;
		zoom.fitMax = max;
	}

	void ResetFlatZoom(FlatZoom& zoom)
	{
		zoom.scale = 1.0f;
		zoom.offset = glm::vec2(0.0f);
	}

	void ClampFlatZoom(FlatZoom& zoom, const FlatFrame& fit)
	{
		const float maxScale = zoom.fixedFrame ? 1.0f : flatMaxScale;
		const float minScale = std::min(zoom.minHalf / std::min(fit.half.x, fit.half.y), maxScale);
		zoom.scale = std::clamp(zoom.scale, minScale, maxScale);
		const glm::vec2 reach = zoom.fixedFrame ? fit.half * (1.0f - zoom.scale) : fit.half;
		zoom.offset = glm::clamp(zoom.offset, -reach, reach);
	}

	void ZoomFlatAt(FlatZoom& zoom, const FlatFrame& fit, glm::vec2 ndc, float factor)
	{
		const FlatFrame before = Zoomed(fit, zoom);
		const glm::vec2 point(before.center.x + ndc.x * before.half.x, before.center.y - ndc.y * before.half.y);
		zoom.scale *= factor;
		ClampFlatZoom(zoom, fit);
		const glm::vec2 half = fit.half * zoom.scale;
		zoom.offset = point - glm::vec2(ndc.x * half.x, -ndc.y * half.y) - fit.center;
		ClampFlatZoom(zoom, fit);
	}

	void PanFlat(FlatZoom& zoom, const FlatFrame& fit, glm::vec2 pixels, float viewportHeight)
	{
		if (zoom.fixedFrame) {
			return;
		}
		//Pixels are square (the frame has the aspect of the view); the cursor moving down moves the map towards +Z on the screen
		const float unitsPerPixel = 2.0f * fit.half.y * zoom.scale / std::max(viewportHeight, 1.0f);
		zoom.offset -= pixels * unitsPerPixel;
		ClampFlatZoom(zoom, fit);
	}
}
