#pragma once

#include "Camera.h"
#include "glm/glm.hpp"

class Player
{
	Player(glm::vec3 spawnPoint, float speed, bool gravity);
	~Player();


	void SetInitialPosition();
	void SteerPlayer(GLFWwindow* window, float deltaTime);
	void Jump();

private:
	glm::vec3 m_Position;
	glm::vec3 m_SpawnPoint;
	float m_Speed;
	bool gravity;

	Camera m_Camera;
};