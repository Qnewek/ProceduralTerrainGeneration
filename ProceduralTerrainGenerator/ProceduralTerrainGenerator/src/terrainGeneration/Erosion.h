#pragma once


namespace erosion {

	struct ErosionConfig {
		//Erosion parameters
		int iterations = 1000;
		float erosionRate = 0.1;
		float sedimentCapacity = 0.01;
		float minSlope = 0.01;
		float gravity = 4.0;
		float inertia = 0.1;
	};

	class Erosion
	{
	public:
		Erosion(int width, int height);
		~Erosion();

		void Erode(float* map);
	private:
		int width, height;
	};
}