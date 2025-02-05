#pragma once

#include "glm/glm.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>

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
public:
	Camera(const unsigned int ScreenWidth, const unsigned int ScreenHeight);
	~Camera();

	glm::vec3 SteerCamera(GLFWwindow* window, float deltaTime, bool yAxisMovement);
	void RotateCamera(GLFWwindow* window);

	glm::mat4* GetViewMatrix();
	glm::mat4* GetProjectionMatrix();
	glm::vec3 GetPosition() const { return position; }

	void EnableMouseControl(GLFWwindow* window);
	void DisableMouseControl(GLFWwindow* window);

	void SetCameraConfig(glm::vec3 position, glm::vec3 front, glm::vec3 up, float yaw, float pitch, float speed, float sensitivity, float fov);
	void SetViewDist(glm::vec2 value) { viewDist = value; }
	void SetYaw(float value) { yaw = value; }
	void SetPitch(float value) { pitch = value; }
	void SetPosition(glm::vec3 value) { position = value; }
	void SetSpeed(float value) { speed = value; initSpeed = value; }
	void SetSensitivity(float value) { sensitivity = value; }
	void SetFov(float value) { fov = value; }
	void SetScreenSize(unsigned int width, unsigned int height) { screenWidth = width; screenHeight = height; }

	float GetYaw() const{ return yaw; }
	float GetPitch() const{ return pitch; }
	float& GetSpeedRef() { return initSpeed; }

private:
	//Screen
	unsigned int screenWidth;
	unsigned int screenHeight;
	//Camera position parameters
	glm::vec3 position;
	glm::vec3 front;
	glm::vec3 up;
	glm::vec3 right;
	//Camera rotation parameters
	float yaw;
	float pitch;
	float xpos;
	float ypos;
	//Camera options
	glm::vec2 viewDist;
	float initSpeed;
	float speed;
	float sensitivity;
	float fov;
	bool mouseControl;

	//Matrices
	glm::mat4 view;
	glm::mat4 projection;

	void UpdateCameraVectors(CameraMovement movement);
};