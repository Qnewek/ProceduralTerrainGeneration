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
	void RotateCamera(GLFWwindow* window);

	glm::mat4* GetViewMatrix();
	glm::mat4* GetProjectionMatrix();
	void EnableMouseControl(GLFWwindow* window);
	void DisableMouseControl(GLFWwindow* window);

	void setYaw(float value) { m_Yaw = value; }
	void setPitch(float value) { m_Pitch = value; }

	float getYaw() const{ return m_Yaw; }
	float getPitch() const{ return m_Pitch; }

private:
	//Screen
	unsigned int m_ScreenWidth;
	unsigned int m_ScreenHeight;
	//Camera position parameters
	glm::vec3 m_Position;
	glm::vec3 m_Front;
	glm::vec3 m_Up;
	glm::vec3 m_Right;
	//Camera rotation parameters
	float m_Yaw;
	float m_Pitch;
	float xpos;
	float ypos;
	//Camera options
	float m_Speed;
	float m_Sensitivity;
	float m_Fov;
	bool mouseControl;

	//Matrices
	glm::mat4 view;
	glm::mat4 projection;

	void UpdateCameraVectors(CameraMovement movement);
};