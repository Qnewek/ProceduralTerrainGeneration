#include "Player.h"

#include <iostream>

Player::Player(const unsigned int ScreenWidth, const unsigned int ScreenHeight, glm::vec3 spawnPoint, float playerHeight, float speed, bool gravity, unsigned int renderDistance) :
	m_Position(spawnPoint), m_SpawnPoint(spawnPoint), m_RenderDistance(renderDistance),
	m_Speed(speed), m_Gravity(gravity), m_PlayerHeight(playerHeight),
	m_Camera(ScreenWidth, ScreenHeight)
{
	m_Camera.setCameraConfig(glm::vec3(m_SpawnPoint.x, m_SpawnPoint.y + this->m_PlayerHeight, m_SpawnPoint.z), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f, speed, 0.1f, 45.0f);
	m_Camera.setViewDist(glm::vec2(0.00001f, 100.0f));
}

Player::~Player()
{
}

void Player::SetPosition(glm::vec3 pos)
{
	this->m_Position = pos;
	this->m_SpawnPoint = pos;
	this->m_Camera.setPosition(glm::vec3(pos.x, pos.y + this->m_PlayerHeight, pos.z));
}

void Player::SteerPlayer(GLFWwindow* window, float* mesh, int stride, float deltaTime)
{
	if (m_Gravity) {
		m_Position = m_Camera.SteerCamera(window, deltaTime, false);

		//TODO: Implement proper movement logic instead of existing one
		if (m_Position.x < 0.0f)
			m_Position.x = 0.0f;
		else if (m_Position.x > 1.0f)
			m_Position.x = 1.0f;

		if (m_Position.z < 0.0f)
			m_Position.z = 0.0f;
		else if (m_Position.z > 1.0f)
			m_Position.z = 1.0f;

		m_Position.y = mesh[static_cast<int>(m_RenderDistance * m_Position.z) * m_RenderDistance + (stride * static_cast<int>(m_RenderDistance * m_Position.x)) + 1];
		std::cout << m_Position.y << std::endl;
		m_Camera.setPosition(glm::vec3(m_Position.x, m_Position.y + this->m_PlayerHeight, m_Position.z));
	}
	else {
		m_Position = m_Camera.SteerCamera(window, deltaTime, true);
	}
}

void Player::Jump()
{
}
