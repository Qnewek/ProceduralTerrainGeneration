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

void FrameMetrics::EndFrame(double frameMs, double cpuMs, glm::vec3 camera, bool holeInView)
{
	frameTimes.Add(frameMs);
	cpuTimes.Add(cpuMs);
	flight.Add(frameMs, camera, holeInView);

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

void FrameMetrics::ImGuiOutput()
{
	double frameAverage = frameTimes.Average();
	ImGui::Text("GPU: %s", gpuName.c_str());
	ImGui::Text("Frame: %.2f ms avg, %.2f ms max (%.0f FPS)  |  CPU: %.2f ms avg", frameAverage, frameTimes.Max(),
		frameAverage > 0.0 ? 1000.0 / frameAverage : 0.0, cpuTimes.Average());
	ImGui::Text("GPU time: scene %.2f ms avg, %.2f ms max, UI %.2f ms avg  |  Scene triangles: %llu",
		sceneGpuTimes.Average(), sceneGpuTimes.Max(), uiGpuTimes.Average(), static_cast<unsigned long long>(sceneTriangles));

	if (ImGui::Button(flight.IsRecording() ? "Stop flight recording" : "Record flight")) {
		if (flight.IsRecording()) {
			flight.Stop();
		}
		else {
			flight.Start();
		}
	}
	ImGui::SetItemTooltip("Records frame times, camera speed and frames with holes in view until stopped");
	if (!flight.Empty()) {
		ImGui::SameLine();
		ImGui::Text("%s%.1f s, %zu frames", flight.IsRecording() ? "recording: " : "", flight.Seconds(), flight.Frames());
		ImGui::Text("Speed: %.0f units/s avg, %.0f max (1 s)  |  FPS: %.1f avg  |  Frame: %.2f ms 99%%, %.2f ms worst",
			flight.AverageSpeed(), flight.MaxSpeed(), flight.AverageFps(), flight.Percentile99Ms(), flight.WorstFrameMs());
		ImGui::Text("Frames over %.0f ms: %zu  |  Frames with holes in view: %zu", FlightRecord::slowFrameMs, flight.SlowFrames(), flight.HoleFrames());
	}
}
