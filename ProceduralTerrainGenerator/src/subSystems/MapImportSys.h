#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "Camera.h"
#include "ExportPanel.h"
#include "GenerationJob.h"
#include "HeightmapImport.h"
#include "LightSource.h"
#include "MapProject.h"
#include "TerrainView.h"
#include "utilities.h"
#include "PanelTabs.h"

//"Imported map" mode: a terrain project (mapproject::Project, .ptgmap) holding one working copy of a map, shown on screen,
//and its original. A map comes from a height map file, a project file or another mode; files are read and written on
//background threads, so operations modifying the map can be added as further steps next to loading.
class MapImportSys
{
public:
	bool Initialize(int _width, int _height, float _heightScale);
	//Called every frame before Draw: installs a map or a project that finished loading, finishes a save
	//@param camera - moved to look down at a project that was opened or a map that was received
	//@param active - Imported map is the mode shown; the camera of another mode is left alone until it is
	void Update(Camera& camera, bool active);
	//true while the project has changes that are not saved
	bool HasUnsavedChanges() const { return dirty && !map.Empty(); }
	//The "Unsaved changes" question; drawn in every mode, because closing the window asks it too
	void ImGuiModals();
	//The window is being closed while the project has unsaved changes: asks first
	void RequestQuit();
	//@return true once, when the window may close (changes discarded or saved)
	bool TakeQuit();
	//A map sent from another mode (heights in metres) becomes a new project, not saved yet; asks first about unsaved changes
	void Receive(mapproject::Project project);

	void Draw(Renderer& renderer, Camera& camera, LightSource& light);
	//true while the 2D map is shown instead of the 3D view
	bool FlatView() const { return renderSettings.map2d; }
	void ReloadShaders() { view.ReloadShader(); }
	//Right panel: the Load tab
	void ImGuiRightPanel();
	//Content of the Display tab of the left panel
	void ImGuiDisplay();
	//Content of the Saving tab: an export pack of the working copy in metres, with the true pixel size
	void ImGuiSaving();
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
		bool reload = false;	//the loaded file read again with another value range
		uint64_t version = 0;	//mapVersion when the load started
	};

	struct ProjectLoad {
		mapproject::Project project;	//maps already in map units
		float metresPerUnit = 1.0f;
		std::filesystem::path path;
		std::string error;
		double ms = 0.0;
		bool ok = false;
		uint64_t version = 0;	//mapVersion when the load started
	};

	struct SaveResult {
		std::filesystem::path path;
		std::string error;
		uintmax_t bytes = 0;
		double ms = 0.0;
		bool ok = false;
		uint64_t version = 0;	//mapVersion of the saved map
	};

	//Something that replaces the project, done at once or after the user answers the "Unsaved changes" question
	struct PendingAction {
		enum class Kind { OPEN, LOAD, RECEIVE, QUIT } kind = Kind::OPEN;
		std::filesystem::path path;						//OPEN, LOAD
		std::optional<mapproject::Project> received;	//RECEIVE
	};
	//Runs the action now if nothing would be lost, otherwise asks first
	void Request(PendingAction action);
	void Run(PendingAction& action);
	void InstallReceived(mapproject::Project& project);

	//Shows a map: values in map units (metres / metresPerUnit)
	void InstallMap(HeightField units, float _metresPerUnit, glm::dvec2 _pixelMetres);
	//Divides the maps by max(|height|, 1 m) over both; @return that divisor (metresPerUnit)
	static float ToUnits(HeightField& working, HeightField& original);
	bool Busy() const { return job.IsRunning() || projectJob.IsRunning() || saveJob.IsRunning(); }

	//Project file
	void SaveProject();
	void SaveProjectAs();
	void StartSaving(const std::filesystem::path& path);
	void OpenProject(const std::filesystem::path& path);
	void BrowseForProject();
	void InstallProject(ProjectLoad& result);
	void FinishSaving(SaveResult& result);
	void RefreshProjects();
	std::filesystem::path ProjectFolder() const;
	std::string ProjectName() const;
	//Content of the Project tab
	void ProjectImGui();
	void SetProjectStatus(const std::string& text, bool isError);

	void RefreshFiles();
	//Lets the user pick a file in the system dialog and selects it
	void BrowseForFile();
	//Reads the header of the chosen file and sets the load options to it
	void Select(const std::filesystem::path& path);
	//@param reload - the loaded file read again with another value range: the project stays, only its map changes
	void StartLoading(const std::filesystem::path& path, bool reload = false);
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
	GenerationJob<ProjectLoad> projectJob;
	GenerationJob<SaveResult> saveJob;
	//Grows with every map shown, so a load started before it is dropped when it finishes
	uint64_t mapVersion = 0;

	//Project state
	std::filesystem::path projectPath;	//empty = not saved yet
	bool dirty = false;
	std::string projectSource;			//mapproject::Project::source
	std::string projectGeneration;		//mapproject::Project::generation
	std::optional<float> seaLevelMetres;
	HeightField original;				//map units like map; empty = the same as map
	std::string suggestedName = "map";	//file name offered by Save as for a project not saved yet
	bool lookDown = false;				//move the camera over the map in the next Update
	char projectFolder[260] = "projects";
	std::vector<std::filesystem::path> projects;
	bool projectsListed = false;
	std::string projectStatus;
	bool projectStatusIsError = false;
	std::optional<PendingAction> pending;	//waiting for the answer to the question
	std::optional<PendingAction> afterSave;	//runs when the save started by the question succeeds
	bool openQuestion = false;
	std::string questionError;	//why the save asked for by the question did not happen, shown when it is asked again
	bool quitApproved = false;
	ExportPanel exporter{ "exports" };

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
