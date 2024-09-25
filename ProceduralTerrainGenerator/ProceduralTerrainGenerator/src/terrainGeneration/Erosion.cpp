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
	Droplet::Droplet(vec2 position, float velocity, float water, float capacity) : position(position), velocity(velocity), water(water), capacity(capacity), direction({ 0, 0 }), dropletLifetime(0), sediment(0.0f)
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

		float dx = this->direction.x * inertia + gradient.x * (1 - inertia);
		float dy = this->direction.y * inertia + gradient.y * (1 - inertia);

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
			this->direction.x = dx / length;
			this->direction.y = dy / length;
		}
		this->adjustPosition();
	}

	void Droplet::adjustPosition()
	{
		//Update the position of the droplet using the formula:
		//posion(new) = position(old) + direction(new)
		//Formula does not take into account the velocity of the droplet in order not to "jump" over the cells
		//This function performs one step which is not representing the real movement of the droplet in a time frame
		this->position.x += this->direction.x;
		this->position.y += this->direction.y;
	}

	void Droplet::adjustVelocity(float elevationDifference, float gravity) {
		//Update the velocity of the droplet as geometric mean using the formula
		//velocity(new) = sqrt(velocity(old)^2 + elevationDifference * gravity)
		//Velocity(old) is squared to give old speed more weight in calculations
		//than the slope of the terrain. elevationDifference is multiplied by gravity to 
		//take gravity into cosideration when calculating the new velocity
		this->velocity = sqrtf((this->velocity * this->velocity) + elevationDifference * gravity);
	}

	void Droplet::evaporate(float evaporationRate) {
		//Update the amount of water the droplet carries by evaporating a percentage of it
		//Formula used: water(new) = water(old) * (1 - evaporationRate)
		this->water *= (1 - evaporationRate);
	}

	float Droplet::adjustCapacity(float minSlope, float erosionRate, float depositionRate, float elevationDifference)
	{
		//Droplet adjusts its capacity based on parameters: water, velocity, minSlope and elevationDifference
		this->capacity = std::max(elevationDifference, minSlope) * this->velocity * this->water * this->capacity;

		//If current sediment is greater than the capacity, the droplet will drop percentage of its surplus sediment
		//based on deposition rate if not it will gather sediment from the map on the previous location
		if (this->sediment > this->capacity) {
			return this->dropSurplusSediment(depositionRate);
		}
		else {
			return -this->gatherSediment(erosionRate, elevationDifference);
		}
	}

	float Droplet::gatherSediment(float erosionRate, float elevationDifference)
	{
		//If the droplet is moving downhill and it carries less sediment than its capacity,
		//it will gather percentage of its remaining capacity adjusted by erosion rate from old position on th map
		//Droplet will add gathered sediment to amount it already carries and return floating point value for 
		//function in erosion class to update the map
		float sedimentTaken = std::min((this->capacity - this->sediment) * erosionRate, elevationDifference);
		this->sediment += sedimentTaken;
		
		return sedimentTaken;
	}

	float Droplet::dropSediment(float elevationDifference)
	{
		//Calculate and drop the right amount of sediment carried by current droplet 
		//in case when the droplet moved uphill thus it passed a gap which needs to be filled
		//If droplet carries enough sediment to fill the gap, it will drop amount of elevationDifference 
		// otherwise it will drop all the sediment it carries
		float dropAmount = std::min(elevationDifference, this->sediment);
		this->sediment -= dropAmount;
		return dropAmount;
	}

	float Droplet::dropSurplusSediment(float depositionRate) {
		//Calculate surplus droplet has to drop and update the amount of sediment it carries
		//taking into account the deposition rate
		float surplusToDrop = (this->sediment - this->capacity) * depositionRate;
		this->sediment -= surplusToDrop;
		return surplusToDrop;
	}
}