#pragma once

//http://www.firespark.de/resources/downloads/implementation%20of%20a%20methode%20for%20hydraulic%20erosion.pdf

namespace erosion {

	struct ErosionConfig {
		//Erosion parameters
		float erosionRate = 0.7f;
		float minSlope = 0.01f;
		float gravity = 10.0f;
		float inertia = 0.3f;
		float depositionRate = 0.2f;
		float evaporationRate = 0.01f;
		int dropletLifetime = 64;
		int erosionRadius = 4;
		float blur = 0.0f;
		//Initial values
		float initialWater = 1.0f;
		float initialVelocity = 0.0f;
		float initialCapacity = 8.0f;
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

		void Erode(float* map);
		void ErodeAndTrace(float* map, float* traceVertices);
		vec2 getGradient(float* map, vec2 pos);
		float getElevationDifference(float* map, vec2 posOld, vec2 posNew);
		float getInterpolatedGridHeight(float* map, vec2 pos);
		void distributeSediment(float* map, vec2 pos, float sedimentDropped);
		float erodeRadius(float* map, vec2 pos, float ammountEroded);
		bool isOnMap(vec2 pos);
		void trackDroplets(float* vertices, float* map, vec2 pos, int step);

		void SetConfig(ErosionConfig config);
		void Resize(int width, int height);

		ErosionConfig& getConfigRef();
		int& getDropletCountRef() { return dropletCount; }
		int getWidth() { return width; }
		int getHeight() { return height; }

	private:
		int width, height;
		int dropletCount = 100;
		ErosionConfig config;
	};

	class Droplet
	{
	public:
		Droplet(vec2 position, float velocity, float water, float capacity);
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