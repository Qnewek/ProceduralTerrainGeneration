#pragma once

#include <optional>


//Implementation of the algorith described here: http://www.firespark.de/resources/downloads/implementation%20of%20a%20methode%20for%20hydraulic%20erosion.pdf
//Its a particle based hydraulic erosion algorithm that simulates the erosion of terrain by water droplets
//The algorithm is implemented in the Erosion class using the Droplet class to simulate the droplets

namespace erosion {
	//Temporary variable for enabling or disabling logging for debugging purposes
	static const bool log = false;

	//Configuration parameters for the erosion
	//@param erosionRate: The rate at which the droplet erodes the terrain
	//@param depositionRate: The rate at which the droplet deposits sediment
	//@param evaporationRate: The rate at which the droplet evaporates
	//@param gravity: The force of gravity on the droplet
	//@param inertia: The inertia of the droplet
	//@param minSlope: The minimum slope for the droplet to move
	//@param erosionRadius: The radius of the droplet
	//@param blur: The blur of the droplet
	//@param dropletLifetime: The lifetime of the droplet
	//@param initialWater: The initial amount of water in the droplet
	//@param initialVelocity: The initial velocity of the droplet
	//@param initialCapacity: The initial capacity of the droplet
	struct ErosionConfig {
		//Erosion parameters
		float erosionRate = 0.2f;
		float depositionRate = 0.5f;
		float evaporationRate = 0.01f;
		
		float gravity = 1.0f;
		float inertia = 0.1f;
		
		float minSlope = 0.0f;
		int erosionRadius = 3;
		float blur = 0.0f;
		
		int dropletLifetime = 64;

		float initialWater = 1.0f;
		float initialVelocity = 1.0f;
		float initialCapacity = 1.0f;
	};

	struct vec2 {
		float x, y;
	};

	struct vec2i_f
	{
		int index;
		float value;
	};

	class Erosion
	{
	public:
		Erosion(int width, int height);
		~Erosion();

		//Simulation functions
		void Erode(std::optional<float*> Track);
		vec2 getGradient(vec2 pos);
		float getElevationDifference(vec2 posOld, vec2 posNew);
		float getInterpolatedGridHeight(vec2 pos);
		void distributeSediment(vec2 pos, float sedimentDropped);
		float erodeRadius(vec2 oldPos, vec2 newPos, float ammountEroded);
		bool isOnMap(vec2 pos);
		void trackDroplets(float* vertices, vec2 pos, int step);

		//Configuration functions
		void SetConfig(ErosionConfig config);
		void Resize(int width, int height);
		void SetMap(float* map);
		void SetDropletCount(int dropletCount);

		//Getters
		ErosionConfig& getConfigRef();
		int& getDropletCountRef() { return dropletCount; }
		int getWidth() { return width; }
		int getHeight() { return height; }
		float* getMap() { return map; }

	private:
		float* map;

		int width, height;
		int dropletCount = 1;

		ErosionConfig config;
	};

	class Droplet
	{
	public:
		Droplet(vec2 position, float velocity, float water, float capacity);
		~Droplet();

		//Getters
		vec2 getPosition() { return position; }
		vec2 getDirection() { return direction; }
		float getVelocity() { return velocity; }
		float getWater() { return water; }
		float getSediment() { return sediment; }
		float getCapacity() { return capacity; }

		//Setters and calculation functions
		void setPosition(vec2 position) { this->position = position; }
		void setDirection(vec2 direction) { this->direction = direction; }
		void adjustDirection(vec2 gradient, float inertia);
		void adjustPosition();
		void adjustVelocity(float elevationDifference, float gravity);
		void adjustSediment(float sedimentCollected);
		void evaporate(float evaporationRate);
		float adjustCapacity(float minSlope, float erosionRate, float depositionRate, float elevationDifference);
		float sedimentToGather(float erosionRate, float elevationDifference);
		float dropSediment(float elevationDifference);
		float dropSurplusSediment(float depositionRate);


	private:
		vec2 position;
		vec2 direction;
		float velocity;
		float water;
		float sediment;
		float capacity;
	};
}