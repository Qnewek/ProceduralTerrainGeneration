#pragma once

#include "Camera.h"
#include "glm/glm.hpp"

class Player
{
public:
	Player(const unsigned int screenWidth, const unsigned int screenHeight, glm::vec3 spawnPoint, float playerHeight, float speed, bool gravity, unsigned int renderDistance);
	~Player();


	void SetPosition(glm::vec3 pos);
	void SteerPlayer(GLFWwindow* window, float* mesh, int stride, float deltaTime);
	void Jump();
	void SetSpeed(float speed);

	Camera* GetCameraRef() { return &camera; };

private:
	glm::vec3 position;
	glm::vec3 spawnPoint;
	unsigned int renderDistance;
	float speed, playerHeight;
	bool gravity;

	Camera camera;
};