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

	void SteerCamera(GLFWwindow* window, float deltaTime);

	void GetViewMatrix(glm::mat4& view);
	void GetProjectionMatrix(glm::mat4& view);
	void EnableMouseControl(GLFWwindow* window);

	void setFirstMouse(bool value) { firstMouse = value; }
	void setYaw(float value) { m_Yaw = value; }
	void setPitch(float value) { m_Pitch = value; }

	bool getFirstMouse() { return firstMouse; }
	float getYaw() { return m_Yaw; }
	float getPitch() { return m_Pitch; }

private:
	//Screen
	unsigned int m_ScreenWidth;
	unsigned int m_ScreenHeight;
	//Camera position parameters
	glm::vec3 m_Position;
	glm::vec3 m_Front;
	glm::vec3 m_Up;
	//Camera rotation parameters
	float m_Yaw;
	float m_Pitch;
	//Camera options
	float m_Speed;
	float m_Sensitivity;
	float m_Fov;
	bool firstMouse;

	void UpdateCameraVectors(CameraMovement movement);
};