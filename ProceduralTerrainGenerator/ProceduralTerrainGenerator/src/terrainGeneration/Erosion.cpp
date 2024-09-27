#include "Erosion.h"

#include <math.h>
#include <random>
#include <queue>
#include <iostream>

namespace erosion {
	//Primitive listNode to use and test erosion
	struct ListNode
	{
		Droplet* d;
		ListNode* next;

		ListNode(vec2 position, float velocity, float water, float capacity) :next(nullptr) {
			d = new Droplet(position, velocity, water, capacity);
		}
		ListNode* deleteNode(ListNode* prev) {
			ListNode* next = this->next;
			if (prev != nullptr) {
				prev->next = next;
			}
			delete this->d;
			delete this;
			return next;
		}
		void deleteAll() {
			ListNode* current = this;
			while (current != nullptr) {
				ListNode* nextNode = current->next;
				delete current->d;
				delete current;
				current = nextNode;
			}
		}
	};

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

	ErosionConfig& Erosion::getConfigRef()
	{
		return config;
	}

	//Terraforming functions
	void Erosion::Erode(float* map)
	{
		ListNode* dropletsHead = nullptr;
		ListNode* dropletPrev = nullptr;
		ListNode* dropletCurrent = nullptr;

		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_real_distribution<float> dist(0.0f, 1.0f);

		//Placeholder node
		dropletsHead = new ListNode({0.0f, 0.0f}, 0.0f, 0.0f, 0.0f);
		dropletCurrent = dropletsHead;
		for (int i = 0; i < dropletCount; i++) {
			//Create a new droplet on random cell on the map
			//Initialise the droplet with initial values cofigured by the user

			dropletCurrent->next = new ListNode({ dist(gen) * width, dist(gen) * height }, config.initialVelocity, config.initialWater, config.initialCapacity);
			dropletCurrent = dropletCurrent->next;
		}

		dropletCurrent = dropletsHead->next;
		dropletPrev = dropletsHead;

		vec2 gradient;
		vec2 oldPosition;
		int fellOff = 0;

		for (int i = 0; i < config.dropletLifetime; i++) {
			if (!dropletsHead->next) {
				break;
			}

			dropletCurrent = dropletsHead->next;
			dropletPrev = dropletsHead;

			while (dropletCurrent) {
				//Calculate the gradient of current cell and adjust the direction of the droplet and its position
				//than check if its still on the map. If it is, calculate the difference in elevation between the old and new position
				gradient = getGradient(map, dropletCurrent->d->getPosition());
				oldPosition = dropletCurrent->d->getPosition();
				dropletCurrent->d->adjustDirection(gradient, config.inertia);

				if (isOnMap(dropletCurrent->d->getPosition())) {
					float deltaElevation = getElevationDifference(map, oldPosition, dropletCurrent->d->getPosition());

					if (deltaElevation >= 0) {
						distributeSediment(map, oldPosition, dropletCurrent->d->dropSediment(deltaElevation));
					}
					else {
						//Calculate new capacity of the current droplet, if its carried sediment surpasses the new capacity
						//Function will return positive number which means that we need to drop some sediment on the old position
						//based on the deposition rate. If the function returns negative number, it means we can erode points in the range
						//of erosion radius and gather possible to collect sediment ammount and add it to the droplet.
						//Function eroding the map in radius takes positive number so after checking a sign of returned value we need
						//to get its absolute. Of course other functions also takes positive number if it comes to deltaElevation
						//so we need to get its absolute value as well.
						deltaElevation = -deltaElevation;
						float sedimentToCollect = dropletCurrent->d->adjustCapacity(config.minSlope, config.erosionRate, config.depositionRate, deltaElevation);

						if (sedimentToCollect > 0.0f) {
							distributeSediment(map, oldPosition, sedimentToCollect);
						}
						else {
							sedimentToCollect = -sedimentToCollect;
							dropletCurrent->d->adjustSediment(erodeRadius(map, oldPosition, sedimentToCollect));
						}

					}
					dropletCurrent->d->adjustVelocity(deltaElevation, config.gravity);
					dropletCurrent->d->evaporate(config.evaporationRate);
					dropletPrev = dropletCurrent;
					dropletCurrent = dropletCurrent->next;
				}
				else {
					dropletCurrent =  dropletCurrent->deleteNode(dropletPrev);
					fellOff++;
				}
			}
		}
		std::cout << "[LOG]Droplets out of the bounds: " << fellOff << std::endl;

		if(dropletsHead)
			dropletsHead->deleteAll();
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

	float Erosion::getInterpolatedGridHeight(float* map, vec2 pos) {
		int x = static_cast<int>(pos.x);
		int y = static_cast<int>(pos.y);
		//These are offsets of the droplet position on the (x,y) cell
		float v = pos.x - x;
		float u = pos.y - y;

		//Interpolate the height of the grid using bilinear interpolation
		//Formula used: H(pos) = P(x, y) * (1 - v) * (1 - u) + P(x+1, y) * v * (1 - u) + P(x, y+1) * (1 - v) * u + P(x+1, y+1) * v * u
		//It is assumed that the erode function checks if the drop fell outside the map
		return (map[y * width + x] * (1 - v) * (1 - u)) + //P(x, y) * (1 - v) * (1 - u) northWest point of the cell
			   (map[y * width + x + 1] * v * (1 - u))   + //P(x+1, y) * v * (1 - u) northEast point of the cell
			   (map[(y + 1) * width + x] * (1 - v) * u) + //P(x, y+1) * (1 - v) * u southWest point of the cell
			   (map[(y + 1) * width + x + 1] * v * u);    //P(x+1, y+1) * v * u southEast point of the cell
	}

	float Erosion::getElevationDifference(float* map, vec2 posOld, vec2 posNew)
	{
		//Calculate the difference in elevation between the old and new position of the droplet
		//Formula used: elevationDifference = elevation(posNew) - elevation(posOld)
		//Its calculated to determine whether the droplet is moving uphill or downhill
		//If the difference is positive, the droplet is moving uphill
		//If the difference is negative, the droplet is moving downhill

		return getInterpolatedGridHeight(map, posOld) - getInterpolatedGridHeight(map, posNew);
	}

	void Erosion::distributeSediment(float* map, vec2 pos, float sedimentDropped) {
		int x = static_cast<int>(pos.x);
		int y = static_cast<int>(pos.y);
		//These are offsets of the droplet position on the (x,y) cell
		float v = pos.x - x;
		float u = pos.y - y;

		//Distribute the sediment dropped by the droplet to the four corners of the cell
		//Its not distributed in the radius of erosion in order to fill a small 1-cell gap
		//There is no need to blur the map via radius
		map[y * width + x] += (1 - v) * (1 - u) * sedimentDropped; //P(x, y) * (1 - v) * (1 - u) northWest point of the cell
		map[y * width + x + 1] += v * (1 - u) * sedimentDropped;   //P(x+1, y) * v * (1 - u) northEast point of the cell
		map[(y + 1) * width + x] += (1 - v) * u * sedimentDropped; //P(x, y+1) * (1 - v) * u southWest point of the cell
		map[(y + 1) * width + x + 1] += v * u * sedimentDropped;   //P(x+1, y+1) * v * u southEast point of the cell
	}

	float Erosion::erodeRadius(float* map, vec2 pos, float ammountEroded) {
		//Erode the terrain in a circular radius around the droplet
		//Its done due to the fact that no thermal erosion or sediment sliding is simulated in this project
		
		float deltax;
		float deltay;
		float weightSum = 0.0f;
		float weight;
		std::queue<vec2i_f> weights;

		for (int y = static_cast<int>(pos.y) - config.erosionRadius; y < static_cast<int>(pos.y) + config.erosionRadius; y++) {
			for (int x = static_cast<int>(pos.x) - config.erosionRadius; x < static_cast<int>(pos.x) + config.erosionRadius; x++) {
				if (x >= 0 && y >= 0 && x < width && y < height) {
					//Calculate the distance from the droplet to the current point on the map
					deltax = x - pos.x;
					deltay = y - pos.y;
					float distance = sqrtf(deltax * deltax + deltay * deltay);
					//Check if the distance is within the erosion radius and if the point has enough material to erode
					if (distance < config.erosionRadius && map[y * width + x] > 0.0f) {
						weight = 1.0f - (distance / config.erosionRadius);
						weightSum += weight;
						weights.push({ y * width + x, weight });
					}
				}
			}
		}

		float totalErosion = 0.0f;
		float possibleErosion;
		float newMapValue;
		while (!weights.empty())
		{
			possibleErosion = ammountEroded * (weights.front().value / weightSum);
			possibleErosion = map[weights.front().index] >= possibleErosion ? possibleErosion : map[weights.front().index];
			newMapValue = map[weights.front().index] - possibleErosion;
			map[weights.front().index] *= config.blur;
			map[weights.front().index] += (1-config.blur) * newMapValue;
			totalErosion += (1-config.blur) * possibleErosion;
			weights.pop();
		}
		return totalErosion;
	}

	bool Erosion::isOnMap(vec2 pos) {
		return pos.x >= 0.0f && pos.y >= 0.0f && pos.x < width - 1.0f && pos.y < height - 1.0f;
	}

	//Droplet class functions
	Droplet::Droplet(vec2 position, float velocity, float water, float capacity) : position(position), velocity(velocity), water(water), capacity(capacity), direction({ 0, 0 }), sediment(0.0f)
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
			return -this->sedimentToGather(erosionRate, elevationDifference);
		}
	}

	float Droplet::sedimentToGather(float erosionRate, float elevationDifference)
	{
		//If the droplet is moving downhill and it carries less sediment than its capacity,
		//it will gather percentage of its remaining capacity adjusted by erosion rate from old position on th map
		//Firstly it will return maximum ammount it can collect and then function in Erosion class will check if 
		//Gathering that ammount of sediment is possible without going below 0 on some P(x,y)
		return std::min((this->capacity - this->sediment) * erosionRate, elevationDifference);
	}

	void Droplet::adjustSediment(float sedimentCollected) {
		this->sediment += sedimentCollected;
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