#include "Camera.h"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

Camera::Camera(const unsigned int ScreenWidth, const unsigned int ScreenHeight) :
	m_ScreenWidth(ScreenWidth), m_ScreenHeight(ScreenHeight),
	m_Position(glm::vec3(0.0f, 0.0f, 3.0f)), m_Front(glm::vec3(0.0f, 0.0f, -1.0f)),
	m_Up(glm::vec3(0.0f, 1.0f, 0.0f)), m_Right(), m_Yaw(-90.0f), m_Pitch(0.0f),
	m_Speed(2.5f), m_Sensitivity(0.1f), m_Fov(45.0f), mouseControl(false), m_init_speed(2.5f),
	view(glm::mat4(1.0f)), projection(glm::mat4(1.0f)), viewDist(glm::vec2(0.1f, 100.0f)),
	ypos(ScreenHeight / 2.0), xpos(ScreenWidth / 2.0)
{
	m_Right = glm::normalize(glm::cross(m_Front, m_Up));
	m_Up = glm::normalize(glm::cross(m_Right, m_Front));
}
Camera::~Camera() {

}
void Camera::setCameraConfig(glm::vec3 position, glm::vec3 front, glm::vec3 up, float yaw, float pitch, float speed, float sensitivity, float fov)
{
	m_Position = position;
	m_Front = front;
	m_Up = up;
	m_Yaw = yaw;
	m_Pitch = pitch;
	m_init_speed = speed;
	m_Sensitivity = sensitivity;
	m_Fov = fov;
	m_Right = glm::normalize(glm::cross(m_Front, m_Up));
	m_Up = glm::normalize(glm::cross(m_Right, m_Front));
}
void Camera::UpdateCameraVectors(CameraMovement movement) {
	if (movement == CameraMovement::FORWARD)
		m_Position += m_Speed * m_Front;
	if (movement == CameraMovement::BACKWARD)
		m_Position -= m_Speed * m_Front;
	if (movement == CameraMovement::LEFT)
		m_Position -= glm::normalize(glm::cross(m_Front, m_Up)) * m_Speed;
	if (movement == CameraMovement::RIGHT)
		m_Position += glm::normalize(glm::cross(m_Front, m_Up)) * m_Speed;
	if (movement == CameraMovement::UP)
		m_Position += m_Speed * m_Up;
	if (movement == CameraMovement::DOWN)
		m_Position -= m_Speed * m_Up;
	if (movement == CameraMovement::ROTATE) {
		m_Front.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
		m_Front.y = sin(glm::radians(m_Pitch));
		m_Front.z = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
		m_Front = glm::normalize(m_Front);
	}
}
glm::vec3 Camera::SteerCamera(GLFWwindow* window, float deltaTime, bool yAxisMovement) {

	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
		DisableMouseControl(window);
	}
	if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
		EnableMouseControl(window);
	}

	m_Speed = static_cast<float>(m_init_speed * deltaTime);
	if (mouseControl)
		RotateCamera(window);
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		UpdateCameraVectors(CameraMovement::FORWARD);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		UpdateCameraVectors(CameraMovement::BACKWARD);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		UpdateCameraVectors(CameraMovement::LEFT);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		UpdateCameraVectors(CameraMovement::RIGHT);
	if (yAxisMovement) {
		if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
			UpdateCameraVectors(CameraMovement::UP);
		if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
			UpdateCameraVectors(CameraMovement::DOWN);
	}

	return m_Position;
}

void Camera::RotateCamera(GLFWwindow* window) {
	double newxpos, newypos;
	glfwGetCursorPos(window, &newxpos, &newypos);

	float xoffset = static_cast<float>(newxpos) - xpos;
	float yoffset = ypos - static_cast<float>(newypos);
	xpos = static_cast<float>(newxpos);
	ypos = static_cast<float>(newypos);

	xoffset *= m_Sensitivity;
	yoffset *= m_Sensitivity;

	m_Yaw += xoffset;
	m_Pitch += yoffset;

	if (m_Pitch > 89.0f)
		m_Pitch = 89.0f;
	if (m_Pitch < -89.0f)
		m_Pitch = -89.0f;

	UpdateCameraVectors(CameraMovement::ROTATE);
}

glm::mat4* Camera::GetViewMatrix() {
	view = glm::mat4(1.0f);
	view = glm::lookAt(m_Position, m_Position + m_Front, m_Up);
	return &view;
}
glm::mat4* Camera::GetProjectionMatrix() {
	projection = glm::mat4(1.0f);
	projection = glm::perspective(glm::radians(m_Fov), (float)m_ScreenWidth / (float)m_ScreenHeight, viewDist.x, viewDist.y);
	return &projection;
}
void Camera::EnableMouseControl(GLFWwindow* window) {
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetCursorPos(window, xpos, ypos);
	mouseControl = true;
}
void Camera::DisableMouseControl(GLFWwindow* window) {
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	mouseControl = false;
}

