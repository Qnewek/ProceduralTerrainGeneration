#include "Player.h"

Player::Player(glm::vec3 spawnPoint, float speed, bool gravity) :
	m_Position(spawnPoint), m_SpawnPoint(spawnPoint), 
	m_Speed(speed), gravity(gravity)
	m_Camera(spawnPoint, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
{

}

Player::~Player()
{
}

void Player::SetInitialPosition()
{
}

void Player::SteerPlayer(GLFWwindow* window, float deltaTime)
{
}

void Player::Jump()
{
}
