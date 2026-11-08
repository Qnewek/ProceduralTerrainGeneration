#pragma once

#include <string>

#include "glm/glm.hpp"

#include "FlightRecord.h"
#include "GpuQuery.h"
#include "RollingStats.h"

//Per-frame performance numbers shown in the Output panel. Needs a current OpenGL context for its whole lifetime.
class FrameMetrics
{
public:
	FrameMetrics();

	void BeginScene();
	void EndScene();
	void BeginUi();
	void EndUi();

	//Records the finished frame and collects GPU results that became ready (they lag a few frames behind)
	//@param frameMs - time between the starts of consecutive frames
	//@param cpuMs - time the main thread spent on the frame, without waiting for the buffer swap
	//@param camera - camera position, for the speed of a recorded flight
	//@param holeInView - part of the view had nothing to draw (chunks not generated yet)
	void EndFrame(double frameMs, double cpuMs, glm::vec3 camera, bool holeInView);

	//Also the flight recording: start/stop button and its results
	void ImGuiOutput();

private:
	static constexpr size_t sampleCount = 120;

	GpuQuery sceneTimeQuery;
	GpuQuery uiTimeQuery;
	GpuQuery sceneTrianglesQuery;

	RollingStats frameTimes{ sampleCount };
	RollingStats cpuTimes{ sampleCount };
	RollingStats sceneGpuTimes{ sampleCount };
	RollingStats uiGpuTimes{ sampleCount };
	GLuint64 sceneTriangles = 0;
	FlightRecord flight;

	std::string gpuName;
};
