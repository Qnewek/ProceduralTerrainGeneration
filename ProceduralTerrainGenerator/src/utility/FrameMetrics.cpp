#include "FrameMetrics.h"

#include "imgui.h"

FrameMetrics::FrameMetrics() : sceneTimeQuery(GL_TIME_ELAPSED), uiTimeQuery(GL_TIME_ELAPSED), sceneTrianglesQuery(GL_PRIMITIVES_GENERATED)
{
	const GLubyte* renderer = glGetString(GL_RENDERER);
	gpuName = renderer ? reinterpret_cast<const char*>(renderer) : "unknown";
}

void FrameMetrics::BeginScene()
{
	sceneTimeQuery.Begin();
	sceneTrianglesQuery.Begin();
}

void FrameMetrics::EndScene()
{
	sceneTrianglesQuery.End();
	sceneTimeQuery.End();
}

void FrameMetrics::BeginUi()
{
	uiTimeQuery.Begin();
}

void FrameMetrics::EndUi()
{
	uiTimeQuery.End();
}

void FrameMetrics::EndFrame(double frameMs, double cpuMs)
{
	frameTimes.Add(frameMs);
	cpuTimes.Add(cpuMs);

	//Time queries return nanoseconds
	GLuint64 nanoseconds = 0;
	if (sceneTimeQuery.Poll(nanoseconds)) {
		sceneGpuTimes.Add(nanoseconds / 1000000.0);
	}
	if (uiTimeQuery.Poll(nanoseconds)) {
		uiGpuTimes.Add(nanoseconds / 1000000.0);
	}
	sceneTrianglesQuery.Poll(sceneTriangles);
}

void FrameMetrics::ImGuiOutput() const
{
	double frameAverage = frameTimes.Average();
	ImGui::Text("GPU: %s", gpuName.c_str());
	ImGui::Text("Frame: %.2f ms avg, %.2f ms max (%.0f FPS)  |  CPU: %.2f ms avg", frameAverage, frameTimes.Max(),
		frameAverage > 0.0 ? 1000.0 / frameAverage : 0.0, cpuTimes.Average());
	ImGui::Text("GPU time: scene %.2f ms avg, %.2f ms max, UI %.2f ms avg  |  Scene triangles: %llu",
		sceneGpuTimes.Average(), sceneGpuTimes.Max(), uiGpuTimes.Average(), static_cast<unsigned long long>(sceneTriangles));
}
