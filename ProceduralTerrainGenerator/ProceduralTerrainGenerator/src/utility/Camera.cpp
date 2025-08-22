#include "Camera.h"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

Camera::Camera(const unsigned int ScreenWidth, const unsigned int ScreenHeight, glm::vec3 _position, float cameraSpeed, float renderDistance) :
	screenWidth(ScreenWidth), screenHeight(ScreenHeight), position(_position),
	up(glm::vec3(0.0f, 1.0f, 0.0f)), front(glm::vec3(0.0f, 0.0f, 1.0f)), right(), yaw(90.0f), pitch(0.0f),
	speed(cameraSpeed), sensitivity(0.1f), fov(45.0f), mouseControl(false), initSpeed(cameraSpeed),
	view(glm::mat4(1.0f)), projection(glm::mat4(1.0f)), viewDist(glm::vec2(0.1f, renderDistance)),
	ypos(ScreenHeight / 2.0), xpos(ScreenWidth / 2.0)
{
	right = glm::normalize(glm::cross(front, up));
	up = glm::normalize(glm::cross(right, front));
}
Camera::~Camera() {

}
void Camera::SetCameraConfig(glm::vec3 position, glm::vec3 front, glm::vec3 up, float yaw, float pitch, float speed, float sensitivity, float fov)
{
	position = position;
	front = front;
	up = up;
	yaw = yaw;
	pitch = pitch;
	initSpeed = speed;
	sensitivity = sensitivity;
	fov = fov;
	right = glm::normalize(glm::cross(front, up));
	up = glm::normalize(glm::cross(right, front));
}
void Camera::UpdateCameraVectors(CameraMovement movement) {
	if (movement == CameraMovement::FORWARD)
		position += speed * front;
	if (movement == CameraMovement::BACKWARD)
		position -= speed * front;
	if (movement == CameraMovement::LEFT)
		position -= glm::normalize(glm::cross(front, up)) * speed;
	if (movement == CameraMovement::RIGHT)
		position += glm::normalize(glm::cross(front, up)) * speed;
	if (movement == CameraMovement::UP)
		position += speed * up;
	if (movement == CameraMovement::DOWN)
		position -= speed * up;
	if (movement == CameraMovement::ROTATE) {
		front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
		front.y = sin(glm::radians(pitch));
		front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
		front = glm::normalize(front);
	}
}
glm::vec3 Camera::SteerCamera(GLFWwindow* window, float deltaTime, bool yAxisMovement) {

	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
		DisableMouseControl(window);
	}
	if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
		EnableMouseControl(window);
	}

	speed = static_cast<float>(initSpeed * deltaTime);
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
	
	return position;
}

void Camera::RotateCamera(GLFWwindow* window) {
	double newxpos, newypos;
	glfwGetCursorPos(window, &newxpos, &newypos);

	float xoffset = static_cast<float>(newxpos) - xpos;
	float yoffset = ypos - static_cast<float>(newypos);
	xpos = static_cast<float>(newxpos);
	ypos = static_cast<float>(newypos);

	xoffset *= sensitivity;
	yoffset *= sensitivity;

	yaw += xoffset;
	pitch += yoffset;

	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	UpdateCameraVectors(CameraMovement::ROTATE);
}

glm::mat4* Camera::GetViewMatrix() {
	view = glm::mat4(1.0f);
	view = glm::lookAt(position, position + front, up);
	return &view;
}
glm::mat4* Camera::GetProjectionMatrix() {
	projection = glm::mat4(1.0f);
	projection = glm::perspective(glm::radians(fov), (float)screenWidth / (float)screenHeight, viewDist.x, viewDist.y);
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
