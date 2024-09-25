#pragma once

//http://www.firespark.de/resources/downloads/implementation%20of%20a%20methode%20for%20hydraulic%20erosion.pdf

namespace erosion {

	struct ErosionConfig {
		//Erosion parameters
		float erosionRate = 0.1;
		float sedimentCapacity = 0.01;
		float minSlope = 0.01;
		float gravity = 4.0;
		float inertia = 0.1;
		float depositionRate = 0.1;
		float evaporationRate = 0.01;
		int dropletLifetime = 30;
		//Initial values
		float initialWater = 1.0;
		float initialVelocity = 0.0;
		float initialCapacity = 0.01;
	};

	struct vec2 {
		float x, y;
	};

	class Erosion
	{
	public:
		Erosion(int width, int height);
		~Erosion();

		void Erode(float* map);
		vec2 getGradient(float* map, vec2 pos);
		float getElevationDifference(float* map, vec2 posOld, vec2 posNew);

		void SetConfig(ErosionConfig config);
		void Resize(int width, int height);

		void SetIterations(int iterations);


		int* getIterationsRef();
		ErosionConfig* getConfigRef();

	private:
		int width, height;
		int iterations = 1000;
		ErosionConfig config;
	};

	class Droplet
	{
	public:
		Droplet(vec2 position, float velocity, float water = 1.0f, float capacity);
		~Droplet();

		vec2 getPosition() { return position; }
		vec2 getDirection() { return direction; }
		float getVelocity() { return velocity; }
		float getWater() { return water; }
		float getSediment() { return sediment; }
		float getCapacity() { return capacity; }

		void setPosition(vec2 position) { this->position = position; }
		void setDirection(vec2 direction) { this->direction = direction; }
		void adjustDirection(vec2 gradient, float inertia);
		void adjustPosition();
		void adjustVelocity(float elevationDifference, float gravity);
		void evaporate(float evaporationRate) { water *= (1 - evaporationRate); }
		float adjustCapacity(float minSlope, float erosionRate, float depositionRate, float elevationDifference);
		float gatherSediment(float erosionRate, float elevationDifference);
		float dropSediment(float elevationDifference);
		float dropSurplusSediment(float depositionRate);


	private:
		vec2 position;
		vec2 direction;
		float velocity;
		float water;
		float sediment;
		float capacity;
		int dropletLifetime;
	};
}