#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "Camera.h"
#include "GenerationJob.h"
#include "HeightmapImport.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "utilities.h"
#include "PanelTabs.h"

//"Imported map" mode: shows a height map read from a file. The map is owned here and read on a background thread,
//so operations modifying it can be added as further steps next to loading.
class MapImportSys
{
public:
	bool Initialize(int _width, int _height, float _heightScale);
	//Called every frame before Draw: installs a map that finished loading
	void Update();

	void Draw(Renderer& renderer, Camera& camera, LightSource& light);
	//true while the 2D map is shown instead of the 3D view
	bool FlatView() const { return renderSettings.map2d; }
	void ReloadShaders() { view.ReloadShader(); }
	//Right panel: the Load tab
	void ImGuiRightPanel();
	//Content of the Display tab of the left panel
	void ImGuiDisplay();
	void ImGuiOutput();

private:
	struct LoadResult {
		HeightField map;		//metres / metresPerUnit
		heightmap::ImportInfo info;
		std::filesystem::path path;
		std::string error;
		float metresPerUnit = 1.0f;
		glm::dvec2 pixelMetres{ 1.0 };	//of the loaded map, after downsampling
		double ms = 0.0;
		bool ok = false;
	};

	void RefreshFiles();
	//Lets the user pick a file in the system dialog and selects it
	void BrowseForFile();
	//Reads the header of the chosen file and sets the load options to it
	void Select(const std::filesystem::path& path);
	void StartLoading(const std::filesystem::path& path);
	void InstallResult(LoadResult& result);
	std::filesystem::path Folder() const;
	//Content of the Load tab: file choice, value range, area, loading
	void LoadImGui();
	//Options of the selected file; @return false when they cannot be loaded
	bool OptionsImGui();
	//Selected file has no units (PNG, R16): its value range is chosen here
	bool NeedsRange() const { return inspected && !header.info.metres; }
	int Downsample() const;
	PanelTabs importTabs;

	TerrainView view;
	TerrainRenderSettings renderSettings;

	//The loaded map; empty until the first successful load. Values are metres / metresPerUnit, so the display modes see about [-1, 1]
	HeightField map;
	heightmap::ImportInfo mapInfo;
	std::filesystem::path mapPath;
	float mapMin = 0.0f, mapMax = 0.0f;
	float metresPerUnit = 64.0f;
	glm::dvec2 pixelMetres{ 1.0 };
	//Height scale showing the map in its true proportions: metresPerUnit * worldUnitsPerTexel / pixel size
	float trueHeightScale = 256.0f;
	double lastLoadMs = 0.0;

	GenerationJob<LoadResult> job;

	//File selection state
	char folder[260] = "exports";
	std::vector<std::filesystem::path> files;
	std::filesystem::path selected;
	bool listed = false;
	//Header of the selected file
	heightmap::FileHeader header;
	bool inspected = false;
	glm::dvec2 filePixelMetres{ 1.0 };	//from the file, before the user changes it
	//Load options of the selected file
	int selectedRange = 0;		//index in the value range list shown in the UI
	float customLowest = 0.0f, customHighest = 1000.0f;	//metres, "Custom (metres)" range
	float pixelSize = 1.0f;		//metres, one pixel of the file
	int area[4] = { 0, 0, 0, 0 };	//x, y, width, height in pixels of the file
	int downsampleIndex = 0;	//index in 1, 2, 4, 8
	std::string error;
};
