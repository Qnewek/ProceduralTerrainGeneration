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
	glm::vec3 GetPosition() const { return m_Position; }

	void EnableMouseControl(GLFWwindow* window);
	void DisableMouseControl(GLFWwindow* window);

	void setCameraConfig(glm::vec3 position, glm::vec3 front, glm::vec3 up, float yaw, float pitch, float speed, float sensitivity, float fov);
	void setViewDist(glm::vec2 value) { viewDist = value; }
	void setYaw(float value) { m_Yaw = value; }
	void setPitch(float value) { m_Pitch = value; }
	void setPosition(glm::vec3 value) { m_Position = value; }
	void setSpeed(float value) { m_Speed = value; }
	void setSensitivity(float value) { m_Sensitivity = value; }
	void setFov(float value) { m_Fov = value; }
	void setScreenSize(unsigned int width, unsigned int height) { m_ScreenWidth = width; m_ScreenHeight = height; }

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
	glm::vec2 viewDist;
	float m_init_speed;
	float m_Speed;
	float m_Sensitivity;
	float m_Fov;
	bool mouseControl;

	//Matrices
	glm::mat4 view;
	glm::mat4 projection;

	void UpdateCameraVectors(CameraMovement movement);
};