#pragma once

#include "glm/glm.hpp"

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
	//Degrees from the horizon on the azimuth side (0) over the zenith (90) to the opposite horizon (180)
	void SetElevation(float degrees) { elevation = degrees; }

	//Unit vector from the ground towards the sun
	glm::vec3 GetDirection() const;
	glm::vec3 GetAmbient() const { return ambient; }
	glm::vec3 GetDiffuse() const { return diffuse; }
	bool IsLightOn() const { return lightOn; }
	//Terrain fades into the sky color near the view distance
	bool IsFogOn() const { return fog; }
	//Distance where the fog begins
	float GetFogStart(float viewDistance) const { return fogStart * viewDistance; }
};
