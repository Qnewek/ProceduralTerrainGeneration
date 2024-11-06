#pragma once

#include "Camera.h"
#include "glm/glm.hpp"

class Player
{
public:
	Player(const unsigned int ScreenWidth, const unsigned int ScreenHeight, glm::vec3 spawnPoint, float playerHeight, float speed, bool gravity, unsigned int renderDistance);
	~Player();


	void SetPosition(glm::vec3 pos);
	void SteerPlayer(GLFWwindow* window, float* mesh, int stride, float deltaTime);
	void Jump();

	Camera* GetCameraRef() { return &m_Camera; };

private:
	glm::vec3 m_Position;
	glm::vec3 m_SpawnPoint;
	unsigned int m_RenderDistance;
	float m_Speed, m_PlayerHeight;
	bool m_Gravity;

	Camera m_Camera;
};