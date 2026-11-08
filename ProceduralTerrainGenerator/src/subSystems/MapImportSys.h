#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Camera.h"
#include "GenerationJob.h"
#include "HeightmapImport.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "utilities.h"

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
	void ImGuiRightPanel();
	void ImGuiLeftPanel();
	void ImGuiOutput();

private:
	struct LoadResult {
		HeightField map;
		heightmap::ImportInfo info;
		std::filesystem::path path;
		std::string error;
		double ms = 0.0;
		bool ok = false;
	};

	void RefreshFiles();
	//Lets the user pick a file in the system dialog and starts loading it
	void BrowseForFile();
	void StartLoading(const std::filesystem::path& path);
	void InstallResult(LoadResult& result);
	std::filesystem::path Folder() const;

	TerrainView view;
	TerrainRenderSettings renderSettings;

	//The loaded map; empty until the first successful load
	HeightField map;
	heightmap::ImportInfo mapInfo;
	std::filesystem::path mapPath;
	float mapMin = 0.0f, mapMax = 0.0f;
	double lastLoadMs = 0.0;

	GenerationJob<LoadResult> job;

	//File selection state
	char folder[260] = "exports";
	std::vector<std::filesystem::path> files;
	std::filesystem::path selected;
	bool listed = false;
	int selectedRange = 0;		//index in the value range list shown in the UI
	std::string error;
};
