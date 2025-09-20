#pragma once

#include "glm/glm.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Shader.h"
#include <memory>

enum class CameraMovement
{
	FORWARD,
	BACKWARD,
	LEFT,
	RIGHT,
	UP,
	DOWN,
	ROTATE
};

class Camera
{
private:
	unsigned int screenWidth, screenHeight;
	float yaw, pitch, xpos, ypos;
	float initSpeed, speed, sensitivity, fov;
	bool mouseControl, anchor = false;

	glm::vec3 position, front, up, right;
	glm::vec2 viewDist;
	glm::mat4 view, projection;

	void UpdateCameraVectors(CameraMovement movement);
public:
	Camera(const unsigned int ScreenWidth, const unsigned int ScreenHeight, glm::vec3 _position, float cameraSpeed, float renderDistance);
	~Camera();

	glm::vec3 SteerCamera(GLFWwindow* window, float deltaTime, bool yAxisMovement);
	void RotateCamera(GLFWwindow* window);
	void ImGuiDraw();
	void ImGuiOutPut();

	glm::mat4* GetViewMatrix();
	glm::mat4* GetProjectionMatrix();
	glm::vec3 GetPosition() const { return position; }

	void EnableMouseControl(GLFWwindow* window);
	void DisableMouseControl(GLFWwindow* window);
	void SetUniforms(Shader& shader);

	void SetCameraConfig(glm::vec3 position, glm::vec3 front, glm::vec3 up, float yaw, float pitch, float speed, float sensitivity, float fov);
	void SetViewDist(glm::vec2 value) { viewDist = value; }
	void SetYaw(float value) { yaw = value; }
	void SetPitch(float value) { pitch = value; }
	void SetPosition(glm::vec3 value) { position = value; }
	void SetSpeed(float value) { speed = value; initSpeed = value; }
	void SetSensitivity(float value) { sensitivity = value; }
	void SetFov(float value) { fov = value; }
	void SetScreenSize(unsigned int width, unsigned int height);

	float GetYaw() const{ return yaw; }
	float GetPitch() const{ return pitch; }
	float& GetSpeedRef() { return initSpeed; }
	float& GetFovRef()  { return fov; }
	float& GetSensitivityRef() { return sensitivity; }
	glm::vec2& GetViewDist() { return viewDist; }
	void CameraAnchor(bool b) { anchor = b; }
};