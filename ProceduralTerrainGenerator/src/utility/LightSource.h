#pragma once

#include "glm/glm.hpp"

//The sun: a directional light that lights the whole world the same way, set by its azimuth and the time of day (elevation),
//with an optional day cycle, and the weather: fog and clouds. Sky computes the sky and the colors of the light for its direction; the fog
//(haze and the fade at the render distance) takes the sky color; Clouds draws the cloud layer, whose shadows fall on everything
class LightSource
{
private:
	float azimuth = 135.0f;		//degrees around the vertical axis, from +X towards +Z
	//Degrees from the horizon on the azimuth side (0, sunrise) over the zenith (90, noon) to the opposite horizon (180, sunset)
	//and under the ground (night), kept in [-90, 270)
	float elevation = 40.0f;
	glm::vec3 ambient = glm::vec3(0.30f, 0.33f, 0.40f);
	glm::vec3 diffuse = glm::vec3(1.0f, 0.95f, 0.85f);
	bool lightOn = true;
	bool fog = true;
	float fogStart = 0.75f;		//share of the view distance where the fade into the sky begins; it is full at the view distance
	float hazeVisibility = 20.0f;	//km at y = 0 where the haze hides all but 2% of the light, 0 = no haze
	float hazeHeight = 200.0f;		//metres up over which the haze thins by e
	bool dayCycle = false;
	float dayMinutes = 10.0f;	//real minutes of a full day
	bool clouds = true;
	float cloudCoverage = 0.45f;	//share of the sky
	float cloudBase = 1200.0f;		//metres above y = 0
	float cloudThickness = 1000.0f;	//metres
	float cloudDensity = 1.0f;
	float cloudShadows = 0.7f;		//share of the sunlight a thick cloud takes away
	float windSpeed = 12.0f;		//metres per second
	float windAzimuth = 30.0f;		//degrees, the direction the wind blows towards, from +X towards +Z
	glm::vec2 windOffset = glm::vec2(0.0f);	//metres the clouds have moved
public:
	void ImGuiDraw();
	//Moves the sun along its path when the day cycle is on, and the clouds with the wind
	//@param seconds - real time since the last call
	void Update(float seconds);
	//Degrees from the horizon on the azimuth side (0) over the zenith (90) to the opposite horizon (180)
	void SetElevation(float degrees);

	//Unit vector from the ground towards the sun
	glm::vec3 GetDirection() const;
	glm::vec3 GetAmbient() const { return ambient; }
	glm::vec3 GetDiffuse() const { return diffuse; }
	bool IsLightOn() const { return lightOn; }
	//Haze and the fade into the sky color near the view distance
	bool IsFogOn() const { return fog; }
	//Distance where the fade into the sky begins
	float GetFogStart(float viewDistance) const { return fogStart * viewDistance; }
	//Haze extinction per world unit at y = 0 and its fall with height per world unit (FrameBlock::hazeDensity / hazeFalloff)
	float GetHazeDensity() const;
	float GetHazeFalloff() const;
	bool AreCloudsOn() const { return clouds; }
	//FrameBlock::cloudLayer / cloudWind
	glm::vec4 GetCloudLayer() const;
	glm::vec4 GetCloudWind() const;
	//Time of day in hours: 6 = sunrise, 12 = noon, 18 = sunset
	float GetTimeOfDay() const { return 6.0f + elevation / 15.0f; }
};
