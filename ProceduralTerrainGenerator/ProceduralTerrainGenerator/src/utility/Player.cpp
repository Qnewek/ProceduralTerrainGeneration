#include "Player.h"

#include <iostream>

Player::Player(const unsigned int screenWidth, const unsigned int screenHeight, glm::vec3 spawnPoint, float playerHeight, float speed, bool gravity, unsigned int renderDistance) :
	position(spawnPoint), spawnPoint(spawnPoint), renderDistance(renderDistance),
	speed(speed), gravity(gravity), playerHeight(playerHeight),
	camera(screenWidth, screenHeight)
{
	camera.SetCameraConfig(glm::vec3(spawnPoint.x, spawnPoint.y, spawnPoint.z), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f, speed, 0.1f, 45.0f);
	camera.SetViewDist(glm::vec2(0.1f, 500.0f));
}

Player::~Player()
{
}

void Player::SetPosition(glm::vec3 pos)
{
	this->position = pos;
	this->spawnPoint = pos;
	this->camera.SetPosition(glm::vec3(pos.x, pos.y, pos.z));
}

void Player::SteerPlayer(GLFWwindow* window, float* mesh, int stride, float deltaTime)
{
	if (gravity) {
		position = camera.SteerCamera(window, deltaTime, false);

		//TODO: Implement proper movement logic instead of existing one
		if (position.x < 0.0f)
			position.x = 0.0f;
		else if (position.x > 1.0f)
			position.x = 1.0f;

		if (position.z < 0.0f)
			position.z = 0.0f;
		else if (position.z > 1.0f)
			position.z = 1.0f;

		position.y = mesh[static_cast<int>(renderDistance * position.z) * renderDistance + (stride * static_cast<int>(renderDistance * position.x)) + 1];
		std::cout << position.y << std::endl;
		camera.SetPosition(glm::vec3(position.x, position.y + this->playerHeight, position.z));
	}
	else {
		position = camera.SteerCamera(window, deltaTime, true);
	}
}

void Player::Jump()
{
}

void Player::SetSpeed(float speed)
{
	speed = speed;
	camera.SetSpeed(speed);
}
