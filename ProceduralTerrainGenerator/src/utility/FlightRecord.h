#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "glm/glm.hpp"

//Statistics of a recorded camera flight, for the Phase 2 exit criterion: speed, frame times and frames with holes in view
class FlightRecord
{
public:
	//A frame is slow when it takes longer than this; on a 60 Hz screen such a frame misses a refresh
	static constexpr double slowFrameMs = 20.0;

	void Start()
	{
		*this = FlightRecord();
		recording = true;
	}
	void Stop() { recording = false; }
	bool IsRecording() const { return recording; }
	bool Empty() const { return frameTimes.empty(); }

	//Adds a frame while recording
	//@param frameMs - time between the starts of consecutive frames
	//@param camera - camera position, world units
	//@param holeInView - part of the view had nothing to draw
	void Add(double frameMs, glm::vec3 camera, bool holeInView)
	{
		if (!recording) {
			return;
		}
		if (hasCamera) {
			const double step = glm::length(camera - lastCamera);
			distance += step;
			windowDistance += step;
			windowSeconds += frameMs / 1000.0;
			//Peak speed over whole seconds; one frame alone gives a noisy speed
			if (windowSeconds >= 1.0) {
				maxSpeed = std::max(maxSpeed, windowDistance / windowSeconds);
				windowDistance = 0.0;
				windowSeconds = 0.0;
			}
		}
		lastCamera = camera;
		hasCamera = true;
		frameTimes.push_back(frameMs);
		totalMs += frameMs;
		holeFrames += holeInView ? 1 : 0;
	}

	size_t Frames() const { return frameTimes.size(); }
	double Seconds() const { return totalMs / 1000.0; }
	//@return 0 without frames
	double AverageFps() const { return totalMs > 0.0 ? 1000.0 * static_cast<double>(frameTimes.size()) / totalMs : 0.0; }
	double WorstFrameMs() const { return frameTimes.empty() ? 0.0 : *std::max_element(frameTimes.begin(), frameTimes.end()); }
	//Frame time that 99% of the frames do not exceed
	double Percentile99Ms() const
	{
		if (frameTimes.empty()) {
			return 0.0;
		}
		std::vector<double> sorted = frameTimes;
		const size_t index = std::min(sorted.size() - 1, sorted.size() * 99 / 100);
		std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(index), sorted.end());
		return sorted[index];
	}
	size_t SlowFrames() const { return static_cast<size_t>(std::count_if(frameTimes.begin(), frameTimes.end(), [](double ms) { return ms > slowFrameMs; })); }
	size_t HoleFrames() const { return holeFrames; }
	//World units per second
	double AverageSpeed() const { return totalMs > 0.0 ? distance / Seconds() : 0.0; }
	//Highest speed over one second, world units per second; 0 for a flight shorter than a second
	double MaxSpeed() const { return maxSpeed; }

private:
	bool recording = false;
	std::vector<double> frameTimes;
	double totalMs = 0.0;
	size_t holeFrames = 0;
	bool hasCamera = false;
	glm::vec3 lastCamera = glm::vec3(0.0f);
	double distance = 0.0, maxSpeed = 0.0;
	double windowDistance = 0.0, windowSeconds = 0.0;
};
