#pragma once

#include <memory>
#include <vector>

#include "glm/glm.hpp"

#include "utilities.h"
#include "TerrainGenerator.h"
#include "BiomeGenerator.h"
#include "GenerationJob.h"
#include "Camera.h"
#include "LightSource.h"
#include "TerrainView.h"

//All maps generated in the "TerrainGen" mode
struct GeneratedWorld {
	TerrainGenerator terrain;
	BiomeGenerator biomes;
	std::vector<glm::vec3> biomeColors;
};

//What has to be regenerated. Requests made while a job runs accumulate and are generated together afterwards
struct WorldRequest {
	bool noises[3] = {};		//terrain component noises, indexed by WorldGenParameter
	bool terrain = false;
	float originx = 0.0f, originy = 0.0f;
	bool biomeNoises[2] = {};	//temperature, humidity
	bool biomes = false;		//biome map and colors

	bool Any() const;
};

class TerrainGenerationSys
{
private:
	struct GenerationResult {
		std::unique_ptr<GeneratedWorld> world;
		WorldRequest done;		//what was actually regenerated (a resize forces more than requested)
		double ms = 0.0;
		bool ok = false;
	};

	bool changeTerrain = false;
	bool biomesGeneration = false;
	bool editNoise = false, editSpline = false, infiniteGeneration = false;
	//true while an edited component noise (OVERLAY layer) is shown instead of the terrain
	bool showPreview = false;
	glm::vec3 oldCamPos = glm::vec3(0.0f);
	double lastGenerationMs = 0.0;

	//Rendering
	TerrainView view;
	TerrainRenderSettings renderSettings;

	//World parameters (map size, terrain, biomes) edited by the UI; generators only read them
	WorldConfig world;

	//Front buffer: the maps shown and read by the UI, touched only by the main thread.
	//A job generates into its own copy (back buffer) which replaces this one when finished
	std::unique_ptr<GeneratedWorld> generated;
	GenerationJob<GenerationResult> job;
	WorldRequest pending;

	//Regenerates the parts of out selected by request (in dependency order) for config.
	//Used both synchronously (Initialize) and on the job thread
	//@param request - in: what to regenerate, out: what was regenerated
	static bool RunGeneration(GeneratedWorld& out, const WorldConfig& config, WorldRequest& request);
	//RunGeneration on maps owned by the caller, timed; the maps are handed back in the result
	static GenerationResult Generate(std::unique_ptr<GeneratedWorld> maps, const WorldConfig& config, WorldRequest request);
	//Starts a job for the pending request on a copy of the front buffer
	void StartJob();
	void InstallResult(GenerationResult& result);
	//Shows the currently edited noise (terrain or biome component) in the OVERLAY layer
	void UpdateOverlay();

	void RequestNoise(WorldGenParameter p);
	void RequestTerrain(float originx, float originy);
	void RequestBiomeNoise(BiomeParameter p);
	void RequestBiomes();

	//Editor UI state
	WorldGenParameter editedComponent = WorldGenParameter::CONTINENTALNESS;
	BiomeParameter editedBiomeComponent = BiomeParameter::TEMPERATURE;
	int samplingResolution = 500;
	SplinePoints splinePlotPoints;	//copy of the edited spline, applied with "Set new spline points"
	bool biomeLevelsChanged = false;	//levels dragged but not applied yet ("Apply biome levels")

	//Pressed editor buttons (0 = nothing selected)
	int noisePressedButton = 0;
	int biomeNoisePressedButton = 0;
	int splinePressedButton = 0;
	int splineDraggedPoint = -1;

public:
	TerrainGenerationSys();
	~TerrainGenerationSys();
	
	bool Initialize(int _width, int _height, float _heightScale);
	//Called every frame before Draw: installs a finished generation and starts the next one if anything is pending
	void Update();

	void Draw(Renderer& renderer, Camera& camera, LightSource& light);
	void ReloadShaders() { view.ReloadShader(); }
	void ImGuiRightPanel();
	void ImGuiLeftPanel();
	void ImGuiOutput(glm::vec3 pos);
	void NoiseEditor();
	void BiomesEditor();
	void BiomeNoisesEditor();
	void SplineEditor();
	void NoisesLevelsForBiomes();
	bool SegmentDrag(std::vector<float>& boundaries, std::string s);
};

