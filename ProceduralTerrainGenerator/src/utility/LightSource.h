#pragma once

#include "glm/glm.hpp"

#include "Shader.h"

//The sun: a directional light that lights the whole world the same way, set by its azimuth and elevation.
//Sky draws the sun disc in this direction and the terrain fog takes the sky color
class LightSource
{
private:
	float azimuth = 135.0f;		//degrees around the vertical axis, from +X towards +Z
	float elevation = 40.0f;	//degrees from the horizon on the azimuth side (0) over the zenith (90) to the opposite horizon (180)
	glm::vec3 ambient = glm::vec3(0.30f, 0.33f, 0.40f);
	glm::vec3 diffuse = glm::vec3(1.0f, 0.95f, 0.85f);
	bool lightOn = true;
	bool fog = true;
	float fogStart = 0.5f;		//share of the view distance where the fog begins; it is full at the view distance
public:
	void ImGuiDraw();

	//Unit vector from the ground towards the sun
	glm::vec3 GetDirection() const;
	bool IsLightOn() const { return lightOn; }
	//Terrain fades into the sky color near the view distance
	bool IsFogOn() const { return fog; }
	//Distance where the fog begins
	float GetFogStart(float viewDistance) const { return fogStart * viewDistance; }

	//Sets light.direction, light.ambient, light.diffuse and lightOn; the shader must be bound
	void SetLightUniforms(Shader& shader) const;
};
