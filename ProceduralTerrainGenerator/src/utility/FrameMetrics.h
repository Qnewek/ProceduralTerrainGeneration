#pragma once

#include <string>

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
	void EndFrame(double frameMs, double cpuMs);

	void ImGuiOutput() const;

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

	std::string gpuName;
};
