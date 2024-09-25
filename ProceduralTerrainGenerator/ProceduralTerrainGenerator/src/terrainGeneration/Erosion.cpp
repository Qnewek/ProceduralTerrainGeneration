#include "Erosion.h"

#include <math.h>
#include <random>

namespace erosion {

	Erosion::Erosion(int width, int height) : width(width), height(height)
	{
	}

	Erosion::~Erosion()
	{
	}

	//Configuration functions
	void Erosion::SetConfig(ErosionConfig config)
	{
		this->config = config;
	}

	void Erosion::Resize(int width, int height)
	{
		this->width = width;
		this->height = height;
	}

	void Erosion::SetIterations(int iterations)
	{
		this->iterations = iterations;
	}

	int* Erosion::getIterationsRef()
	{
		return &iterations;
	}

	ErosionConfig* Erosion::getConfigRef()
	{
		return &config;
	}

	//Terraforming functions
	void Erosion::Erode(float* map)
	{

	}

	vec2 Erosion::getGradient(float* map, vec2 pos)
	{
		vec2 gradient = { 0, 0 };
		//Calculation offset of the droplet  on the (x,y)cell
		float v = pos.x - static_cast<int>(pos.x);
		float u = pos.y - static_cast<int>(pos.y);
		//Get the integer part of the position ( x and y coordinates)
		int x0 = static_cast<int>(pos.x);
		int y0 = static_cast<int>(pos.y);

		//It is assumed that erode function checks if the drop fell outside the map
		//Formula used: g(pos) = ( (P(x+1, y) - P(x, y)) * (1 - v) + (P(x+1, y+1) - P(x, y+1)) * v )
		//						 ( (P(x, y+1) - P(x, y)) * (1 - u) + (P(x+1, y+1) - P(x+1, y)) * u )
		gradient.x = (map[y0 * width + x0 + 1] - map[y0 * width + x0]) * (1 - v) +
					 ((map[(y0 + 1) * width + x0 + 1] - map[(y0 + 1) * width + x0]) * v);
		gradient.y = (map[(y0 + 1) * width + x0] - map[y0 * width + x0]) * (1 - u) +
					 ((map[(y0 + 1) * width + x0 + 1] - map[y0 * width + x0 + 1]) * u);

		return gradient;
	}

	float Erosion::getElevationDifference(float* map, vec2 posOld, vec2 posNew)
	{
		//Calculate the difference in elevation between the old and new position of the droplet
		//Formula used: elevationDifference = elevation(posNew) - elevation(posOld)
		//Its calculated to determine whether the droplet is moving uphill or downhill
		//If the difference is positive, the droplet is moving uphill
		//If the difference is negative, the droplet is moving downhill
		int x0 = static_cast<int>(posOld.x);
		int y0 = static_cast<int>(posOld.y);
		int x1 = static_cast<int>(posNew.x);
		int y1 = static_cast<int>(posNew.y);

		return map[y0 * width + x0] - map[y1 * width + x1];
	}

	//Droplet class functions
	Droplet::Droplet(vec2 position, float velocity, float water, float sediment, float capacity) : position(position), velocity(velocity), water(water), sediment(sediment), capacity(capacity)
	{
	}

	Droplet::~Droplet()
	{
	}

	void Droplet::adjustDirection(vec2 gradient, float inertia)
	{
		//Calculate the direction of the droplet using the formula: 
		//direction(new) = direction(old) * inertia + gradient * (1 - inertia)
		//New direction is obtained by blending old position with the gradient, with inertia parameter
		//being taken into account as blending strength parameter
		//Inertia being close to 1 means the droplet will move in the same direction as before
		//Inertia being close to 0 means the droplet will move in the direction of the gradient

		float dx = direction.x * inertia + gradient.x * (1 - inertia);
		float dy = direction.y * inertia + gradient.y * (1 - inertia);

		//If the direction is zero which means droplet wouldnt move, generate a random direction
		if (dx == 0 && dy == 0)
		{
			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

			dx = dist(gen);
			dy = dist(gen);
		}

		//Normalize the direction to get a unit vector
		float length = sqrtf((dx * dx) + (dy * dy));

		if (length > 0)
		{
			direction.x = dx / length;
			direction.y = dy / length;
		}
		this->adjustPosition();
	}

	void Droplet::adjustPosition()
	{
		//Update the position of the droplet using the formula:
		//posion(new) = position(old) + direction(new)
		//Formula does not take into account the velocity of the droplet in order not to "jump" over the cells
		//This function performs one step which is not representing the real movement of the droplet in a time frame
		position.x += direction.x;
		position.y += direction.y;
	}
}