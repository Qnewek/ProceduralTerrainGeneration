#pragma once
#include <memory>

#include "Noise.h"
#include "Erosion.h"
#include "GenerationJob.h"
#include "Camera.h"
#include "utilities.h"
#include "LightSource.h"
#include "TerrainView.h"

//All maps generated in the "Noise heightmap" mode
struct NoiseMaps {
	noise::SimplexNoiseClass noise;
	erosion::Erosion erosion{ 1, 1 };
};

//What has to be regenerated. Requests made while a job runs accumulate and are generated together afterwards
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
		struct GenerationResult {
			std::unique_ptr<NoiseMaps> maps;
			NoiseRequest done;		//what was actually regenerated (a resize forces more than requested)
			double ms = 0.0;
			bool ok = false;
		};

		//Map size, noise and erosion parameters edited by the UI; generators only read them
		NoiseMapConfig config;

		bool erosionDraw = false, instantUpdate = true, infiniteGeneration = false;
		double lastGenerationMs = 0.0;
		glm::vec3 oldCamPos = glm::vec3(0.0f, 0.0f, 0.0f);

		//Rendering
		TerrainView view;
		TerrainRenderSettings renderSettings;

		//Front buffer: the maps shown and read by the UI, touched only by the main thread.
		//A job generates into its own copy (back buffer) which replaces this one when finished
		std::unique_ptr<NoiseMaps> generated;
		GenerationJob<GenerationResult> job;
		NoiseRequest pending;

		//Regenerates the parts of out selected by request for the given config.
		//Used both synchronously (Initialize) and on the job thread
		//@param request - in: what to regenerate, out: what was regenerated
		static bool RunGeneration(NoiseMaps& out, const NoiseMapConfig& _config, NoiseRequest& request);
		//RunGeneration on maps owned by the caller, timed; the maps are handed back in the result
		static GenerationResult Generate(std::unique_ptr<NoiseMaps> maps, const NoiseMapConfig& _config, NoiseRequest request);
		//Starts a job for the pending request on a copy of the front buffer
		void StartJob();
		void InstallResult(GenerationResult& result);

		//New noise map; the erosion shown next to it is dropped and starts from the new map
		void RequestNoise(float originx, float originy);
		void RequestErosion();
	public:
		NoiseBasedGenerationSys();
		~NoiseBasedGenerationSys();

		bool Initialize(int _width, int _height, float _heightScale);
		//Called every frame before Draw: installs a finished generation and starts the next one if anything is pending
		void Update();

		void Draw(Renderer& renderer, Camera& camera, LightSource& light);
		void ReloadShaders() { view.ReloadShader(); }
		void ImGuiRightPanel();
		void ImGuiLeftPanel();
		void ErosionImGui();
		void ImGuiOutput();
};

