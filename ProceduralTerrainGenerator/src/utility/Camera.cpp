#include "Camera.h"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "imgui.h"

Camera::Camera(const unsigned int ScreenWidth, const unsigned int ScreenHeight, glm::vec3 _position, float cameraSpeed, float renderDistance) :
	screenWidth(ScreenWidth), screenHeight(ScreenHeight), position(_position),
	up(glm::vec3(0.0f, 1.0f, 0.0f)), front(glm::vec3(0.0f, 0.0f, 1.0f)), right(), yaw(90.0f), pitch(0.0f),
	speed(cameraSpeed), sensitivity(0.1f), fov(45.0f), mouseControl(false), initSpeed(cameraSpeed),
	view(glm::mat4(1.0f)), projection(glm::mat4(1.0f)), viewDist(glm::vec2(0.1f, renderDistance)),
	ypos(ScreenHeight / 2.0f), xpos(ScreenWidth / 2.0f)
{
	right = glm::normalize(glm::cross(front, up));
	up = glm::normalize(glm::cross(right, front));
}
Camera::~Camera() {

}
void Camera::SetCameraConfig(glm::vec3 _position, glm::vec3 _front, glm::vec3 _up, float _yaw, float _pitch, float _speed, float _sensitivity, float _fov)
{
	position = _position;
	front = _front;
	up = _up;
	yaw = _yaw;
	pitch = _pitch;
	initSpeed = _speed;
	speed = _speed;
	sensitivity = _sensitivity;
	fov = _fov;
	right = glm::normalize(glm::cross(front, up));
	up = glm::normalize(glm::cross(right, front));
}
void Camera::LookAt(glm::vec3 _position, glm::vec3 target)
{
	position = _position;
	const glm::vec3 direction = target - position;
	if (glm::length(direction) < 1e-4f) {
		return;
	}
	const glm::vec3 d = glm::normalize(direction);
	//Inverse of the ROTATE formula in UpdateCameraVectors
	yaw = glm::degrees(std::atan2(d.z, d.x));
	pitch = glm::clamp(glm::degrees(std::asin(d.y)), -89.0f, 89.0f);
	UpdateCameraVectors(CameraMovement::ROTATE);
}

void Camera::SetScreenSize(unsigned int width, unsigned int height)
{
	if (width != screenWidth) {
		screenWidth = width;
		xpos = width / 2.0f;
	}
	if (height != screenHeight) {
		screenHeight = height;
		ypos = height / 2.0f;
	}
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

	//Keys typed into an ImGui text field must not steer the camera
	const bool typing = ImGui::GetIO().WantTextInput;

	if (!typing && glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
		DisableMouseControl(window);
	}
	if (!typing && glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
		EnableMouseControl(window);
	}

	speed = static_cast<float>(initSpeed * deltaTime);
	if (mouseControl)
		RotateCamera(window);
	if (typing)
		return position;
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

void Camera::ImGuiDraw() {
	if (ImGui::CollapsingHeader("Camera settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Camera position", glm::value_ptr(position), 1.0f);
		ImGui::SliderFloat("Camera speed", &initSpeed, 0.1f, 200.0f);
		ImGui::SliderFloat("Camera FOV", &fov, 1.0f, 120.0f);
		ImGui::SliderFloat("Camera sensitivity", &sensitivity, 0.01f, 1.0f);
		//Chunks of the infinite world get coarser with distance, so they can be seen far away
		ImGui::SliderFloat("Render distance", &viewDist.y, 1.0f, 50000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
	}
}

void Camera::ImGuiOutPut()
{
	ImGui::Text("Press 'm' to enable rotation of the camera using mouse, press 'ESC' to exit mouse control");
	ImGui::Text("Press 'w', 'a', 's', 'd' to move camera, 'space' to move up, 'ctrl' to move down");
	ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", this->GetPosition().x, this->GetPosition().y, this->GetPosition().z);
}

glm::mat4 Camera::ComputeView() const
{
	return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::ComputeProjection() const
{
	//The near plane moves away with a long render distance, or the depth buffer would lack precision far away
	return glm::perspective(glm::radians(fov), (float)screenWidth / (float)screenHeight, std::max(viewDist.x, viewDist.y / 20000.0f), viewDist.y);
}

glm::mat4* Camera::GetViewMatrix() {
	view = ComputeView();
	return &view;
}
glm::mat4* Camera::GetProjectionMatrix() {
	projection = ComputeProjection();
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

void Camera::SetUniforms(Shader& shader)
{
	shader.SetProjection(*GetProjectionMatrix());
	shader.SetView(*GetViewMatrix());
	shader.SetViewPos(position);
}
