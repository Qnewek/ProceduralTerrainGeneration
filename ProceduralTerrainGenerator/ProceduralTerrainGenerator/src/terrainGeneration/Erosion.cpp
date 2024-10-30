#include "Erosion.h"

#include <math.h>
#include <random>
#include <queue>
#include <iostream>

namespace erosion {
	//Primitive listNode to use and test erosion
	//TODO: Implement a more efficient data structure
	struct ListNode
	{
		Droplet* d;
		ListNode* next;

		ListNode(vec2 position, float velocity, float water, float capacity) : d(new Droplet(position, velocity, water, capacity)), next(nullptr) {}

		~ListNode() {
			delete d;
		}

		ListNode* deleteNode(ListNode* prev) {
			ListNode* next = this->next;
			if (prev != nullptr) {
				prev->next = next;
			}
			delete this;
			return next;
		}

		void deleteAll() {
			ListNode* current = this;
			while (current != nullptr) {
				ListNode* nextNode = current->next;
				delete current;
				current = nextNode;
			}
		}
	};

	Erosion::Erosion(int width, int height) : width(width), height(height), map(nullptr)
	{
	}

	Erosion::~Erosion()
	{
	}

	//--------------------------------------------------------------------------------------
	//Configuration functions
	//--------------------------------------------------------------------------------------

	//Set the configuration of the erosion
	//@param config - ErosionConfig struct containing all the parameters of the erosion
	void Erosion::SetConfig(ErosionConfig config)
	{
		this->config = config;
	}

	//Set the number of droplets to be simulated
	//@param dropletCount - number of droplets to be simulated
	void Erosion::SetDropletCount(int dropletCount)
	{
		this->dropletCount = dropletCount;
	}
	
	//Resizes the map to the new dimensions
	//@param width - new width of the map
	//@param height - new height of the map
	void Erosion::Resize(int width, int height)
	{
		this->width = width;
		this->height = height;
	}

	//Set the heightsMap to be eroded
	//@param _map - pointer to the map to be eroded
	void Erosion::SetMap(float* _map)
	{
		delete[] map;
		this->map = new float[width * height];

		std::copy(_map, _map + (width * height), this->map);
	}

	//Get the reference to the configuration of the erosion
	//@return reference to the ErosionConfig struct
	ErosionConfig& Erosion::getConfigRef()
	{
		return config;
	}

	//--------------------------------------------------------------------------------------
	//Simulation functions
	//--------------------------------------------------------------------------------------

	//Track the path of the droplet
	//@param vertices - pointer to the array of vertices to store the path of the droplet
	//@param pos - position of the droplet
	//@param step - step of the simulation (basically auxiliary index to save the pos of the function)
	void Erosion::trackDroplets(float* vertices, vec2 pos, int step) {
		vertices[step * 3] = pos.x / width;
		vertices[step * 3 + 1] = getInterpolatedGridHeight(pos);
		vertices[step * 3 + 2] = pos.y / height;
	}

	//Main simulation function
	//@param Track - optional pointer to the array of vertices to store the path of the droplet (pass std::nullopt to disable)
	void Erosion::Erode(std::optional<float*> Track)
	{
		ListNode* dropletsHead = nullptr;
		ListNode* dropletPrev = nullptr;
		ListNode* dropletCurrent = nullptr;

		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_real_distribution<float> dist(0.0f, 1.0f);
				
		vec2 gradient;
		vec2 oldPosition;

		int fellOff = 0;
		int step = 0;

		//Placeholder node
		dropletsHead = new ListNode({ 0.0f, 0.0f }, 0.0f, 0.0f, 0.0f);
		dropletCurrent = dropletsHead;

		//Creatint a new droplets on a random cell on the map
		//Initialize the droplet with initial values cofigured by the user
		for (int i = 0; i < dropletCount; i++) {
			dropletCurrent->next = new ListNode({ dist(gen) * width, dist(gen) * height }, config.initialVelocity, config.initialWater, config.initialCapacity);
			dropletCurrent = dropletCurrent->next;

			//If tracking enabled, save the droplets initial positions
			if(Track.has_value() && Track.value())
				trackDroplets(Track.value(), dropletCurrent->d->getPosition(), step++);
		}

		dropletCurrent = dropletsHead->next;
		dropletPrev = dropletsHead;

		for (int i = 0; i < config.dropletLifetime; i++) {
			if (!dropletsHead->next) {
				break;
			}

			dropletCurrent = dropletsHead->next;
			dropletPrev = dropletsHead;

			while (dropletCurrent) {
				if (log) {
					std::cout << "------------------------------------\n";
				}

				//Calculate the gradient of current cell and adjust the direction of the droplet and its position
				gradient = getGradient(dropletCurrent->d->getPosition());
				oldPosition = dropletCurrent->d->getPosition();
				dropletCurrent->d->adjustDirection(gradient, config.inertia);
				
				//If tracking enabled, save the droplets path
				if (Track.has_value() && Track.value())
					trackDroplets(Track.value(), dropletCurrent->d->getPosition(), step++);

				//Check if the droplet is still on the map
				if (isOnMap(dropletCurrent->d->getPosition())) {
					//Calculate the difference in elevation between the old and new position of the droplet
					float deltaElevation = getElevationDifference(oldPosition, dropletCurrent->d->getPosition());
					
					if (log) {
						std::cout << "[LOG] Elevation difference: " << deltaElevation << std::endl;
					}

					//If the droplet is moving uphill, it will drop some sediment on the old cell 
					//in order to fill the gap whit the droplet passed, otherwise it will erode the terrain
					//based on the calculated values or drop surplus sediment if it surpasses the capacity of the droplet
					if (deltaElevation >= 0.0f) {
						if(log){
							std::cout << "[LOG] Droplet is moving uphill\n";
						}
						distributeSediment(oldPosition, dropletCurrent->d->dropSediment(deltaElevation));
					}
					else {
						if (log){
								std::cout << "[LOG] Droplet is moving downhill\n";
						}
						//Calculate new capacity of the current droplet, if its carried sediment surpasses the new capacity
						//function will return positive number which means that we need to drop some sediment on the old position
						//based on the deposition rate. If the function returns negative number, it means we can erode points in the range
						//of erosion radius and gather possible to collect sediment ammount and add it to the droplet.
						float sedimentToCollect = dropletCurrent->d->adjustCapacity(config.minSlope, config.erosionRate, config.depositionRate, deltaElevation);
						
						if (sedimentToCollect > 0.0f) {
							distributeSediment(oldPosition, sedimentToCollect);
						}
						else {
							sedimentToCollect = -sedimentToCollect;
							dropletCurrent->d->adjustSediment(erodeRadius(oldPosition, dropletCurrent->d->getPosition(), sedimentToCollect));
						}

					}
					dropletCurrent->d->adjustVelocity(deltaElevation, config.gravity);
					dropletCurrent->d->evaporate(config.evaporationRate);
					dropletPrev = dropletCurrent;
					dropletCurrent = dropletCurrent->next;
				}
				else {
					//If the droplet fell off the map, delete it from the list
					dropletCurrent = dropletCurrent->deleteNode(dropletPrev);
					fellOff++;
				}
			}
		}
		std::cout << "[LOG] Droplets out of the map: " << fellOff << std::endl;

		if (dropletsHead)
			dropletsHead->deleteAll();
	}

	vec2 Erosion::getGradient(vec2 pos)
	{
		vec2 gradient = { 0, 0 };

		//Get the integer part of the position ( x and y coordinates)
		int x = static_cast<int>(pos.x);
		int y = static_cast<int>(pos.y);

		//Calculation offset of the droplet  on the (x,y)cell
		float v = pos.x - x;
		float u = pos.y - y;

		//It is assumed that erode function checks if the drop fell outside the map
		//Formula used: g(pos) = ( (P(x+1, y) - P(x, y)) * (1 - v) + (P(x+1, y+1) - P(x, y+1)) * v )
		//						 ( (P(x, y+1) - P(x, y)) * (1 - u) + (P(x+1, y+1) - P(x+1, y)) * u )
		gradient.x = (map[y * width + x + 1] - map[y * width + x]) * (1 - v) +
					 ((map[(y + 1) * width + x + 1] - map[(y + 1) * width + x]) * v);
		gradient.y = (map[(y + 1) * width + x] - map[y * width + x]) * (1 - u) +
					 ((map[(y + 1) * width + x + 1] - map[y * width + x + 1]) * u);

		if (log) {
			std::cout << "[LOG] Gradient: " << gradient.x << " " << gradient.y << std::endl;
		}
		return gradient;
	}

	float Erosion::getInterpolatedGridHeight(vec2 pos) {
		//Get the integer part of the position ( x and y coordinates)
		int x = static_cast<int>(pos.x);
		int y = static_cast<int>(pos.y);

		//Calculation offset of the droplet  on the (x,y)cell
		float v = pos.x - x;
		float u = pos.y - y;

		//Interpolate the height of the grid using bilinear interpolation
		//Formula used: H(pos) = P(x, y) * (1 - v) * (1 - u) + P(x+1, y) * v * (1 - u) + P(x, y+1) * (1 - v) * u + P(x+1, y+1) * v * u
		//It is assumed that the erode function checks if the drop fell outside the map
		float diff ((map[y * width + x] * (1 - v) * (1 - u)) + //P(x, y) * (1 - v) * (1 - u) northWest point of the cell
					(map[y * width + x + 1] * v * (1 - u))   + //P(x+1, y) * v * (1 - u) northEast point of the cell
					(map[(y + 1) * width + x] * (1 - v) * u) + //P(x, y+1) * (1 - v) * u southWest point of the cell
					(map[(y + 1) * width + x + 1] * v * u));   //P(x+1, y+1) * v * u southEast point of the cell
		return diff;
	}

	float Erosion::getElevationDifference(vec2 posOld, vec2 posNew)
	{
		//Calculate the difference in elevation between the old and new position of the droplet
		//Formula used: elevationDifference = elevation(posNew) - elevation(posOld)
		//Its calculated to determine whether the droplet is moving uphill or downhill
		//If the difference is positive, the droplet is moving uphill
		//If the difference is negative, the droplet is moving downhill

		float old = getInterpolatedGridHeight(posOld);
		float newP = getInterpolatedGridHeight(posNew);

		if (log) {
			std::cout << "[LOG] Old: " << old << " New: " << newP << std::endl;
		}

		return newP - old;
	}

	void Erosion::distributeSediment(vec2 pos, float sedimentDropped) {
		int x = static_cast<int>(pos.x);
		int y = static_cast<int>(pos.y);

		//Calculation offset of the droplet  on the (x,y)cell
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

	float Erosion::erodeRadius(vec2 oldPos, vec2 newPos, float ammountEroded) {
		//Erode the terrain in a circular radius around the droplet
		//Its done due to the fact that no thermal erosion or sediment sliding is simulated in this project
		//In order to perform mentioned above action we need to calculate weights of each point within the radius
		
		float deltax;
		float deltay;
		float weightSum = 0.0f;
		float weight;
		std::queue<vec2i_f> weights;

		for (int y = static_cast<int>(oldPos.y) - config.erosionRadius; y < static_cast<int>(oldPos.y) + config.erosionRadius; y++) {
			for (int x = static_cast<int>(oldPos.x) - config.erosionRadius; x < static_cast<int>(oldPos.x) + config.erosionRadius; x++) {
				if (x >= 0 && y >= 0 && x < width && y < height) {
					//Calculate the distance from the droplet to the current point on the map
					deltax = x - oldPos.x;
					deltay = y - oldPos.y;
					float distance = sqrtf(deltax * deltax + deltay * deltay);

					//Check if the distance is within the erosion radius and if the point is higher than new position
					if (distance < config.erosionRadius && map[y * width + x] > map[static_cast<int>(newPos.y) * width + static_cast<int>(newPos.x)]) {
						weight = 1.0f - (distance / config.erosionRadius);
						weightSum += weight;
						weights.push({ y * width + x, weight });
					}
				}
			}
		}

		if (log) {
			std::cout << "---------------Eroding-Radius---------------\n"
						 "Number of points in the radius: " << weights.size() << std::endl;
		}

		float totalErosion = 0.0f;
		float possibleErosion;
		float newMapValue;

		//Erode the points in the radius of the droplet
		//Based on blur parameter, value of the new point is interpolated between the old value and the eroded value
		//Blur value 0.0 means that the new value is the eroded value, 
		//blur value 1.0 means that the new value is the old value
		while (!weights.empty())
		{
			possibleErosion = ammountEroded * (weights.front().value / weightSum);
			possibleErosion = map[weights.front().index] >= possibleErosion ? possibleErosion : map[weights.front().index];
			newMapValue = map[weights.front().index] - possibleErosion;
			map[weights.front().index] *= config.blur;
			map[weights.front().index] += (1-config.blur) * newMapValue;
			totalErosion += (1-config.blur) * possibleErosion;
			weights.pop();

			if (log) {
				std::cout << "[LOG] Eroded: " << possibleErosion << std::endl;
			}
		}

		if (log) {
			std::cout << "--------------------------------------------\n";
		}
		return totalErosion;
	}

	//Check if the droplet is still on the map
	//@param pos - position of the droplet
	bool Erosion::isOnMap(vec2 pos) {
		return pos.x >= 0.0f && pos.y >= 0.0f && pos.x < width - 1.0f && pos.y < height - 1.0f;
	}

	//--------------------------------------------------------------------------------------
	//Droplet class functions
	//--------------------------------------------------------------------------------------

	Droplet::Droplet(vec2 position, float velocity, float water, float capacity) : position(position), velocity(velocity), water(water), capacity(capacity), direction({ 0, 0 }), sediment(0.0f)
	{
	}

	Droplet::~Droplet()
	{
	}

	//Adjust the direction of the droplet based on the gradient of the current cell
	//@param gradient - gradient of the current cell
	//@param inertia - inertia parameter
	void Droplet::adjustDirection(vec2 gradient, float inertia)
	{
		//Calculate the direction of the droplet using the formula: 
		//direction(new) = direction(old) * inertia + gradient * (1 - inertia)
		//New direction is obtained by blending old position with the gradient, with inertia parameter
		//being taken into account as blending strength parameter
		//Inertia being close to 1 means the droplet will move in the same direction as before
		//Inertia being close to 0 means the droplet will move in the direction of the gradient
		float dx = this->direction.x * inertia - gradient.x * (1 - inertia);
		float dy = this->direction.y * inertia - gradient.y * (1 - inertia);

		//If the direction is zero which means the droplet wouldnt move, move it in a random direction
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

	//Update the position of the droplet using the formula:
	//posion(new) = position(old) + direction(new)
	//Formula does not take into account the velocity of the droplet in order not to "jump" over the cells
	//This function performs one step which is not representing the real movement of the droplet in a time frame
	void Droplet::adjustPosition()
	{
		this->position.x += this->direction.x;
		this->position.y += this->direction.y;
	}

	//Update the velocity of the droplet as geometric mean using the formula
	//velocity(new) = sqrt(velocity(old)^2 + elevationDifference * gravity)
	//Velocity(old) is squared to give old speed more weight in calculations
	//than the slope of the terrain. elevationDifference is multiplied by gravity to 
	//take gravity into cosideration when calculating the new velocity
	void Droplet::adjustVelocity(float elevationDifference, float gravity) {
		this->velocity = sqrtf((this->velocity * this->velocity) + (elevationDifference * gravity));
		if (log) {
			std::cout << "[LOG] Velocity: " << this->velocity << std::endl;
		}
	}

	//Update the amount of water the droplet carries by evaporating a percentage of it
	//Formula used: water(new) = water(old) * (1 - evaporationRate)
	//@param evaporationRate - rate at which the droplet evaporates
	void Droplet::evaporate(float evaporationRate) {
		this->water *= (1 - evaporationRate);
		if (log) {
			std::cout << "[LOG] Water: " << this->water << std::endl;
		}
	}

	//Droplet adjusts its capacity based on parameters: water, velocity, minSlope and elevationDifference
	//If current sediment is greater than the capacity, the droplet will drop percentage of its surplus sediment
	//based on deposition rate if not it will gather sediment from the map on the previous location
	//@param minSlope - minimum slope for the droplet to move
	//@param erosionRate - rate at which the droplet erodes the terrain
	//@param depositionRate - rate at which the droplet deposits sediment
	//@param elevationDifference - difference in elevation between the old and new position of the droplet
	//@return - the amount of sediment the droplet gathered or dropped
	float Droplet::adjustCapacity(float minSlope, float erosionRate, float depositionRate, float elevationDifference)
	{
		this->capacity = std::max(-elevationDifference, minSlope) * this->velocity * this->water;

		if (log) {
			std::cout << "[LOG] Capacity: " << this->capacity << std::endl;
		}

		if (this->sediment > this->capacity) {
			return this->dropSurplusSediment(depositionRate);
		}
		else {
			return -this->sedimentToGather(erosionRate, elevationDifference);
		}
	}

	//Calculate the ammount of sediment the droplet can gather based on the erosion rate and elevation difference
	//@param erosionRate - rate at which the droplet erodes the terrain
	//@param elevationDifference - difference in elevation between the old and new position of the droplet
	//@return - the ammount of sediment the droplet can gather
	float Droplet::sedimentToGather(float erosionRate, float elevationDifference)
	{
		//If the droplet is moving downhill and it carries less sediment than its capacity,
		//it will gather percentage of its remaining capacity adjusted by erosion rate from old position on th map
		//Firstly it will return maximum ammount it can collect and then function in Erosion class will check if 
		//Gathering that ammount of sediment is possible without going below 0 on some P(x,y)
		float sedimentToGather = std::min((this->capacity - this->sediment) * erosionRate, -elevationDifference);
		if (log) {
			std::cout << "[LOG] Sediment: " << this->sediment << std::endl;
			std::cout << "[LOG] Sediment to gather: " << sedimentToGather << std::endl;
		}

		return sedimentToGather;
	}

	//Adjust the amount of sediment the droplet carries by adding the sediment gathered from the map
	//@param sedimentCollected - the amount of sediment the droplet gathered
	void Droplet::adjustSediment(float sedimentCollected) {
		this->sediment += sedimentCollected;
	}

	//Calculate and drop the right amount of sediment carried by current droplet 
	//in case when the droplet moved uphill thus it passed a gap which needs to be filled
	//If droplet carries enough sediment to fill the gap, it will drop amount of elevationDifference 
	//otherwise it will drop all the sediment it carries
	//@param elevationDifference - difference in elevation between the old and new position of the droplet
	//@return - the amount of sediment the droplet dropped
	float Droplet::dropSediment(float elevationDifference)
	{
		float dropAmount = std::min(elevationDifference, this->sediment);
		this->sediment -= dropAmount;

		if (log) {
			std::cout << "[LOG] Dropped: " << dropAmount << std::endl;
		}

		return dropAmount;
	}

	//Calculate and drop the surplus sediment the droplet carries
	//@param depositionRate - rate at which the droplet deposits sediment
	//@return - the amount of sediment the droplet dropped
	float Droplet::dropSurplusSediment(float depositionRate) {
		//Calculate surplus droplet has to drop and update the amount of sediment it carries
		//taking into account the deposition rate
		float surplusToDrop = (this->sediment - this->capacity) * depositionRate;
		this->sediment -= surplusToDrop;

		if (log) {
			std::cout << "[LOG] Surplus to drop: " << surplusToDrop << std::endl;
		}

		return surplusToDrop;
	}
}