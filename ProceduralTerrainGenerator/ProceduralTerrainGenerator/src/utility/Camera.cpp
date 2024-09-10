#include "Camera.h"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

Camera::Camera(const unsigned int ScreenWidth, const unsigned int ScreenHeight) :
	m_ScreenWidth(ScreenWidth), m_ScreenHeight(ScreenHeight),
	m_Position(glm::vec3(0.0f, 0.0f, 3.0f)), m_Front(glm::vec3(0.0f, 0.0f, -1.0f)),
	m_Up(glm::vec3(0.0f, 1.0f, 0.0f)), m_Yaw(-90.0f), m_Pitch(0.0f),
	m_Speed(0.0f), m_Sensitivity(0.1f), m_Fov(45.0f), firstMouse(true)
{

}
Camera::~Camera() {

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
void Camera::SteerCamera(GLFWwindow* window, float deltaTime) {

	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	m_Speed = static_cast<float>(2.5 * deltaTime);
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		UpdateCameraVectors(CameraMovement::FORWARD);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		UpdateCameraVectors(CameraMovement::BACKWARD);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		UpdateCameraVectors(CameraMovement::LEFT);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		UpdateCameraVectors(CameraMovement::RIGHT);
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		UpdateCameraVectors(CameraMovement::UP);
	if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
		UpdateCameraVectors(CameraMovement::DOWN);
}

void Camera::GetViewMatrix(glm::mat4& view) {
	view = glm::lookAt(m_Position, m_Position + m_Front, m_Up);
}
void Camera::GetProjectionMatrix(glm::mat4& projection) {
	projection = glm::perspective(glm::radians(m_Fov), (float)m_ScreenWidth / (float)m_ScreenHeight, 0.1f, 100.0f);
}
void Camera::EnableMouseControl(GLFWwindow* window) {
	//glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	//glfwSetCursorPosCallback(window, mouse_callback);
}
/*void mouse_callback(GLFWwindow* window, double xposIn, double yposIn)
{
	float xpos = static_cast<float>(xposIn);
	float ypos = static_cast<float>(yposIn);

	if ()
	{
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	}

	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top
	lastX = xpos;
	lastY = ypos;

	float sensitivity = 0.1f; // change this value to your liking
	xoffset *= sensitivity;
	yoffset *= sensitivity;

	yaw += xoffset;
	pitch += yoffset;

	// make sure that when pitch is out of bounds, screen doesn't get flipped
	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	glm::vec3 front;
	front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	front.y = sin(glm::radians(pitch));
	front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
	cameraFront = glm::normalize(front);
}*/