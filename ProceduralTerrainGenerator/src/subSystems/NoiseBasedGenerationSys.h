#pragma once
#include <memory>

#include "Erosion.h"
#include "GenerationJob.h"
#include "GpuHeightGenerator.h"
#include "Camera.h"
#include "utilities.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "PresetPanel.h"
#include "ExportPanel.h"

//What has to be done. The noise map is generated on the GPU at the next Update;
//erosion runs on the CPU in a job, and requests made while it runs are done together afterwards
struct NoiseRequest {
	bool noise = false;
	float originx = 0.0f, originy = 0.0f;
	bool resetErosion = false;	//the next erosion starts from the noise map instead of accumulating
	bool erode = false;

	bool Any() const { return noise || resetErosion || erode; }
};

class NoiseBasedGenerationSys
{
	private:
		struct ErosionResult {
			std::unique_ptr<erosion::Erosion> erosion;
			NoiseRequest done;
			double ms = 0.0;
		};

		//Map size, noise and erosion parameters edited by the UI; generators only read them
		NoiseMapConfig config;

		bool erosionDraw = false, instantUpdate = true, infiniteGeneration = false;
		double lastGenerationMs = 0.0, lastErosionMs = 0.0;
		glm::vec3 oldCamPos = glm::vec3(0.0f, 0.0f, 0.0f);

		//Rendering; the noise map lives in the TERRAIN layer of the view
		TerrainView view;
		TerrainRenderSettings renderSettings;
		GpuHeightGenerator gpu;

		//Eroded map shown and read by the UI, touched only by the main thread.
		//A job works on its own copy, which replaces this one when finished
		std::unique_ptr<erosion::Erosion> eroded;
		GenerationJob<ErosionResult> job;
		NoiseRequest pending;

		//Runs on the job thread
		//@param base - the noise map, used when erosion starts from it
		static ErosionResult RunErosion(std::unique_ptr<erosion::Erosion> maps, HeightField base, const erosion::ErosionConfig& erosionConfig, NoiseRequest request);
		//Starts a job for the pending erosion request on a copy of the eroded map
		void StartJob();
		void InstallResult(ErosionResult& result);

		//New noise map; the erosion shown next to it is dropped and starts from the new map
		void RequestNoise(float originx, float originy);
		void RequestErosion();

		void PresetsImGui();
		//Replaces the parameters and regenerates the map; an erosion waiting to start is dropped
		void ApplyConfig(const NoiseMapConfig& loaded);
		PresetPanel presets{ "presets/noise" };
		ExportPanel exporter{ "exports" };
	public:
		NoiseBasedGenerationSys();
		~NoiseBasedGenerationSys();

		bool Initialize(int _width, int _height, float _heightScale);
		//Called every frame before Draw: generates a requested map, installs a finished erosion and starts the next one
		void Update();

		void Draw(Renderer& renderer, Camera& camera, LightSource& light);
		void ReloadShaders() { view.ReloadShader(); gpu.ReloadShaders(); }
		void ImGuiRightPanel();
		void ImGuiLeftPanel();
		void ErosionImGui();
		void ImGuiOutput();
};

