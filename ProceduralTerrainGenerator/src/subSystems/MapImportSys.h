#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "glm/glm.hpp"
#include "AsyncRead.h"
#include "imgui.h"

#include "BiomeEditor.h"
#include "Camera.h"
#include "ExportPanel.h"
#include "GenerationJob.h"
#include "GpuErosion.h"
#include "GpuHeightGenerator.h"
#include "GpuSculpt.h"
#include "HeightmapImport.h"
#include "Hydrology.h"
#include "LandCover.h"
#include "LightSource.h"
#include "MapBiomes.h"
#include "MapEdit.h"
#include "MapProject.h"
#include "RiverMap.h"
#include "TerrainView.h"
#include "VegetationExport.h"
#include "VegetationLayer.h"
#include "WaterSurface.h"
#include "utilities.h"
#include "PanelTabs.h"

class TerrainMaterials;
class VegetationModels;

//"Imported map" mode: a terrain project (mapproject::Project, .ptgmap) holding one working copy of a map, shown on screen,
//and its original. A map comes from a height map file, a project file or another mode; files are read and written on
//background threads, so operations modifying the map can be added as further steps next to loading.
class MapImportSys
{
public:
	MapImportSys() = default;
	//Stops a running placement of plants early, so closing the app does not wait for all of it
	~MapImportSys();
	MapImportSys(const MapImportSys&) = delete;
	MapImportSys& operator=(const MapImportSys&) = delete;

	bool Initialize(int _width, int _height, float _heightScale);
	//Called every frame before Draw: installs a map or a project that finished loading, finishes a save
	//@param camera - moved to look down at a project that was opened or a map that was received
	//@param active - Imported map is the mode shown; the camera of another mode is left alone until it is
	//@param deltaTime - seconds since the last frame (the pace of the brushes)
	void Update(Camera& camera, bool active, float deltaTime);
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
	//@return true once, when a received map was installed (at once, or after the "Unsaved changes" question): the app shows this mode then
	bool TakeReceived() { return std::exchange(receivedInstalled, false); }

	void Draw(Renderer& renderer, Camera& camera, LightSource& light);
	//true while the 2D map is shown instead of the 3D view
	bool FlatView() const { return renderSettings.map2d; }
	void ReloadShaders()
	{
		view.ReloadShader(); erosion.ReloadShaders(); water.ReloadShader(); sculptor.ReloadShaders(); biomeGenerator.ReloadShaders(); vegetation.ReloadShader(); placedLayer.ReloadShader();
		biomesStale = true;
	}
	//Right panel: the Project, Edit and Load tabs; also the mouse input of the brushes and their outline on the terrain
	void ImGuiRightPanel();
	//Content of the Display tab of the left panel
	void ImGuiDisplay();
	//Content of the Saving tab: an export pack of the working copy in metres, with the true pixel size
	void ImGuiSaving();
	void ImGuiOutput();

	//Names of the surface materials in the order of their ids, for the biome editor
	void SetMaterialNames(std::vector<std::string> names) { materialNames = names; biomeEditor.SetMaterialNames(std::move(names)); }
	//Models the plants of the biome editor can name and the map grows; nullptr = none (the models are being released)
	void SetVegetationModels(VegetationModels* models);
	//Material list whose textures the export pack copies; nullptr = none
	void SetTerrainMaterials(const TerrainMaterials* materials) { terrainMaterials = materials; }

private:
	const TerrainMaterials* terrainMaterials = nullptr;
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

	//Shows a new map (values in map units, metres / metresPerUnit) and clears the history
	void InstallMap(HeightField units, float _metresPerUnit, glm::dvec2 _pixelMetres);
	//Shows a map in map units; the true proportions follow it, the history stays
	void ShowUnits(HeightField units, float _metresPerUnit, glm::dvec2 _pixelMetres);

	//Edits of the working copy (Edit tab)
	struct EditResult {
		HeightField metres;
		std::string label;		//of the history step
		uint64_t version = 0;	//mapVersion when the edit started
		double ms = 0.0;
		std::string error;
	};
	//The working copy in metres
	HeightField WorkingMetres() const;
	//Shows an edited map in metres without a history step: map units again from the largest height of it and of the original, the same
	//vertical exaggeration, a change to save; the original keeps its heights
	void ShowMetres(HeightField metres);
	//An operation finished: before goes to the history, the map and the sea level become the new state
	void ApplyEdit(HeightField metres, std::optional<float> seaLevel, mapedit::Step before);
	//The sea level changed (a history step without a map)
	void SetSeaLevel(std::optional<float> metres, std::optional<float> before);
	//Undo (redo = false) or redo the last step
	void StepHistory(bool redo);
	//Runs an operation on a copy of the working copy in metres on a background thread
	void StartEdit(const std::string& label, std::function<void(HeightField&)> operation);
	void FinishEdit(EditResult& result);
	//The map or the sea level changed: what is computed from them (rivers) is out of date
	//The plants of the old heights are not shown on the new ones (a placement still running is dropped)
	void OnMapChanged() { mapChanged = true; climateStale = true; PlantsChanged(); }
	void SetEditStatus(const std::string& text, bool isError);
	//Content of the Edit tab
	void EditImGui();
	void HistoryImGui();
	//Divides the maps by max(|height|, 1 m) over both; @return that divisor (metresPerUnit)
	static float ToUnits(HeightField& working, HeightField& original);
	bool Busy() const
	{
		return job.IsRunning() || projectJob.IsRunning() || saveJob.IsRunning() || editJob.IsRunning() || stroke.has_value() || paintStroke.has_value()
			|| plantStroke.has_value();
	}

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
	//Where the pixels of the map lie (from its file, its project or "Take georeference from file"); none = not known
	std::optional<geo::GeoReference> georeference;
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
	bool receivedInstalled = false;	//TakeReceived
	ExportPanel exporter{ "exports" };

	//Edit state
	mapedit::History history{ size_t(1024) << 20 };
	int historyMegabytes = 1024;
	std::string historyNote;		//why the last step is not in the history
	GenerationJob<EditResult> editJob;
	bool mapChanged = false;		//rivers have to be computed again
	std::string editStatus;
	bool editStatusIsError = false;
	int smoothRadius = 4;
	float smoothStrength = 1.0f;
	float scaleFactor = 1.0f;
	float scaleOffset = 0.0f;		//metres
	float seaBefore = 0.0f;			//sea level when dragging its slider started
	//Erosion on the GPU, in erosion units (mapedit::ErosionScale), run in Update
	GpuErosion erosion;
	erosion::ErosionConfig erosionConfig = WorldConfig::DefaultWorldErosion();
	bool erodeRequested = false;
	uint32_t erosionRuns = 0;
	//Erodes the working copy now (GL thread)
	void Erode();

	//Sculpt brushes (MapImportSculpt.cpp). While a stroke runs the map lives only on the GPU (the TERRAIN layer); map keeps the state
	//from before it, and letting the button go reads the layer back as one history step
	//The map before the stroke is the CPU map itself (Busy() keeps it unchanged until the stroke ends), so a press copies nothing
	struct Stroke {
		float target = 0.0f;	//FLATTEN: map units under the cursor where the stroke started
		uint32_t seed = 0;		//NOISE
		bool painted = false;	//a dab was applied; a click without one leaves no history step
		float limit = 1.5f;		//largest |height| in map units the stroke may paint; the terrain culling gets the same bound
	};
	sculpt::BrushConfig brush;
	//BrushConfig::strength of the two kinds of brushes, kept apart (they have different units)
	float raiseRate = 10.0f;	//RAISE, LOWER, NOISE: metres per second
	float smoothRate = 2.0f;	//SMOOTH, FLATTEN: part of the way per second
	bool sculptOn = false;
	GpuSculpt sculptor;
	//Map texel under the cursor, from the depth of the scene drawn in the last frame
	std::optional<glm::vec2> hoverTexel;
	//The depth under the cursor, read without waiting for the GPU: the texel of a frame comes from the read of an earlier one, with the matrices,
	//viewport and map size it was started with
	AsyncRead depthRead;
	struct DepthPick {
		glm::ivec2 pixel = glm::ivec2(0);
		glm::mat4 viewProjection = glm::mat4(1.0f);
		glm::ivec4 viewport = glm::ivec4(0);
		glm::ivec2 mapSize = glm::ivec2(0);
		int frame = 0;		//ImGui frame the read started in; older than maxPickAge frames = dropped (the mode was left meanwhile)
	} depthPick;
	static constexpr int maxPickAge = 3;
	std::optional<glm::vec2> lastPick;
	std::optional<Stroke> stroke;
	uint32_t strokeSeed = 0;
	//Mouse input of the last ImGui frame (Update runs before the next one): the left button went down over the scene, is held
	bool paintPressed = false, paintHeld = false, paintInvert = false;
	bool strokeCancelPressed = false;	//Esc during a sculpt stroke: back to the map before it
	//Outline of the brush on the terrain, ImGui coordinates, and the viewport it is clipped to
	std::vector<ImVec2> brushOutline;
	ImVec2 outlineClipMin, outlineClipMax;
	//Starts, continues or ends a stroke (GL thread)
	void UpdateSculpt(const Camera& camera, bool active, float deltaTime);
	//Ends a stroke: the layer read back becomes a history step
	void FinishStroke();
	void CancelStroke();
	//After the terrain is drawn: the texel under the cursor and the outline of the brush around it
	void PickUnderCursor(Camera& camera, LightSource& light);
	//Mouse input and the outline; every frame of the mode, inside the ImGui frame
	void SculptInputImGui();
	//The Sculpt section of the Edit tab
	void SculptImGui();

	//Rivers and lakes of the working copy (hydrology::ComputeOnMap), computed on a background thread after every change; not carved
	struct RiverResult {
		hydrology::MapRivers rivers;
		uint64_t version = 0;	//mapVersion when the computation started
		double ms = 0.0;
	};
	rivers::RiverConfig riverConfig;	//method NONE = off, REGION = on
	GenerationJob<RiverResult> riverJob;
	//Heights in metres / hydrology::mapMetresPerHeight; shared (never changed in place) so the placement of plants reads it without a copy
	std::shared_ptr<const hydrology::MapRivers> riverNetwork;
	double riverMs = 0.0;
	RiverMap riverMap;
	WaterSurface water;
	bool RiversOn() const { return riverConfig.method == rivers::Method::REGION; }
	//The river network shown belongs to the map as it is now (none wanted, or computed after its last change)
	bool RiversCurrent() const { return !RiversOn() || (!mapChanged && !riverJob.IsRunning()); }
	//Takes a finished network and starts a new one when the map changed; called every frame
	void UpdateRivers();
	//Water of the network to the GPU, levels in map units
	void UploadRiverWater();
	void RiversImGui();

	//Biomes of the working copy (MapImportBiomes.cpp): continentalness and mountainousness from the map on a background thread
	//(mapbiomes::ComputeClimate) after every change of it, then the biome colors and the splat map on the GPU (GenerateMapBiomes)
	struct ClimateResult {
		mapbiomes::ClimateMaps maps;
		uint64_t version = 0;	//mapVersion when the computation started
		double ms = 0.0;
		std::string error;		//the computation failed (out of memory); maps empty
	};
	mapbiomes::Settings biomeSettings = mapbiomes::Default(std::nullopt);
	BiomeEditor biomeEditor;
	std::vector<std::string> materialNames;	//in the order of their ids, for the export of the splat map
	GpuHeightGenerator biomeGenerator;
	std::unique_ptr<TextureClass> climateTexture;	//GL_RG16F, the last climate computed
	std::unique_ptr<TextureClass> biomeIds;			//GL_R32I; read only for an export
	GenerationJob<ClimateResult> climateJob;
	bool climateStale = false;		//the map, the sea or the climate settings changed: continentalness and mountainousness again
	bool biomesStale = false;		//the biome table, climate, noises or materials changed: the GPU pass again
	double climateMs = 0.0;
	std::string biomeStatus;
	bool biomeStatusIsError = false;
	int biomeNoise = 0;				//noise edited in the Biomes tab: 0 temperature, 1 humidity
	std::vector<std::filesystem::path> biomePresets;
	bool biomePresetsListed = false;
	int biomePreset = 0;
	bool BiomesOn() const { return biomeSettings.enabled; }
	//Land cover (stage D3, the Land cover section of the Biomes tab): the class map of the files of biomeSettings, computed on a background
	//thread after a new map, a change of the files or of the georeference; edits of the terrain keep it. classTexture (GL_R8UI) is the class
	//map of the map shown (reset with a new map), the GPU pass uses it with the masks of mapbiomes::ClassMasks
	struct LandCoverResult {
		landcover::ClassMap map;
		uint64_t mapId = 0, filesVersion = 0;	//landCoverMapId and landCoverFilesVersion when it started
		double ms = 0.0;
		std::string error;		//out of memory; map empty
	};
	GenerationJob<LandCoverResult> landCoverJob;
	std::unique_ptr<TextureClass> classTexture;
	std::vector<landcover::FileStatus> landCoverFiles;	//status of each file of the last class map
	std::array<uint64_t, 256> classCounts{};			//map pixels of each class of the last class map
	bool landCoverStale = false;
	uint64_t landCoverMapId = 0;		//changes with every new map (not with edits)
	uint64_t landCoverFilesVersion = 0;	//changes with the files and the georeference
	double landCoverMs = 0.0;
	std::string landCoverError;
	bool LandCoverOn() const { return BiomesOn() && biomeSettings.source == mapbiomes::Source::LAND_COVER; }
	//A class map of the map shown exists: the LAND COVER display mode can show it
	bool LandCoverShown() const { return LandCoverOn() && classTexture && classTexture->GetWidth() == map.GetWidth() && classTexture->GetHeight() == map.GetHeight(); }
	//The files or the georeference changed: the class map again, a running one is dropped
	void LandCoverInputChanged() { landCoverFilesVersion++; landCoverStale = true; }
	//Takes a finished class map, starts a new one; called every frame (GL thread)
	void UpdateLandCover();
	//The Biome source and Land cover sections of the Biomes tab
	void LandCoverImGui();
	//Biomes painted over the automatic ones (stage D2, the Paint section of the Biomes tab, MapImportBiomes.cpp). The layer is 0x0 until
	//the first stroke (nothing painted, no memory), then the size of the map; paintTexture (GL_RG8UI) is its copy on the GPU, present
	//exactly when the layer has a size. A stroke changes the layer on the CPU, uploads the rectangle of each dab and runs the GPU pass
	//over it; letting the button go is one history step with the layer from before
	biomepaint::Layer biomePaint;
	std::unique_ptr<TextureClass> paintTexture;
	struct PaintStroke {
		biomepaint::Layer before;	//for the history
		uint8_t slot = 0;			//slot of the biome painted
		bool painted = false;		//a dab changed a texel; a click without one leaves no history step
	};
	std::optional<PaintStroke> paintStroke;
	bool biomePaintOn = false;
	std::string paintBiome;			//name of the biome painted, from the table
	float paintRadius = 32.0f;		//map texels
	float paintHardness = 0.5f;
	float paintRate = 2.0f;			//strength (0 to 1) per second at the centre
	uint32_t paintSeed = 0;			//biomepaint::Dab::seed, another one every dab
	//Shows another painted layer (undo, a project, 0x0 = nothing painted): the whole layer to the GPU and the biomes again
	void UsePaint(biomepaint::Layer layer);
	//Starts, continues or ends a stroke of the biome brush (GL thread, from UpdateSculpt)
	void UpdateBiomePaint(const Camera& camera, bool active, float deltaTime);
	//Ends a stroke of the biome brush: one history step
	void FinishPaintStroke();
	//Runs the GPU pass of the biomes over a rectangle (width < 0 = the whole map) when the climate is ready for this map
	//@return false if it could not run
	bool RunBiomePass(glm::ivec4 region);
	//The Paint section of the Biomes tab
	void PaintImGui();
	//A brush follows the cursor: Sculpt, the biome brush or the plant brush
	bool BrushOn() const { return sculptOn || (biomePaintOn && BiomesOn()) || plantBrushOn; }
	float ActiveBrushRadius() const
	{
		return plantBrushOn ? plantRadius / static_cast<float>(pixelMetres.x) : biomePaintOn ? paintRadius : brush.radius;
	}
	//Takes a finished climate, starts a new one, runs the GPU pass; called every frame (GL thread)
	void UpdateBiomes();
	//Settings of a new map: from the JSON of a project (empty = defaults for the sea level, keeping the land cover source, files and classes
	//of the previous settings); @return why they were ignored, empty if not
	//@param folderOfProject - folder of the project read (land cover files moved together with it)
	std::string UseBiomeSettings(const std::string& json, const std::filesystem::path& folderOfProject = {});
	//Display mode and splat map after biomes were turned on or off
	void ShowBiomesState();
	//Content of the Biomes tab
	void BiomesImGui();
	//The settings of the climate from the terrain; sets climateStale or biomesStale for what changed
	//@return true if one changed
	bool ClimateSourceImGui();
	//A new georeference of the map (system chosen, taken from a file): the project changes, the land cover is read again
	void SetGeoreference(std::optional<geo::GeoReference> reference);
	//The georeference of the map: system, corner, its system to choose and "Take georeference from file..."
	void GeoreferenceImGui();

	//Plants of the biomes (stage D4, MapImportVegetation.cpp): placed on a background thread in metres of the map pixel
	//(vegexport::PlaceMapLimited, at most vegexport::maxMapInstances) after every pass of the biomes and every change of what they
	//depend on, never during a stroke; drawn as the extra set of their own VegetationLayer
	struct PlantResult {
		std::vector<vegetation::Instance> instances;
		vegexport::Kept kept = vegexport::Kept::ALL;
		size_t count = 0;
		uint64_t version = 0;	//plantVersion when it started
		double ms = 0.0;
		std::string error;		//out of memory; instances empty
	};
	VegetationModels* vegetationModels = nullptr;
	VegetationLayer vegetation;
	mapproject::VegetationSettings plantSettings;
	GenerationJob<PlantResult> plantJob;
	bool plantsStale = false;
	uint64_t plantVersion = 0;		//grows with every change; a placement started with an older one is dropped
	std::shared_ptr<std::atomic<bool>> plantCancel;	//of the running placement; set by a change so it stops early
	uint64_t biomeIdsVersion = 0;	//mapVersion of the last pass of the biomes: ids of another map are never planted
	//Plants of the biome table and ready models the last placement started with; a change places again
	std::vector<std::vector<biome::Plant>> placedTablePlants;
	int placedReadyModels = -1;
	vegexport::Kept plantKept = vegexport::Kept::ALL;
	size_t plantCount = 0;
	double plantMs = 0.0;
	std::string plantStatus;
	bool plantStatusIsError = false;
	bool PlantsOn() const { return BiomesOn() && plantSettings.enabled; }
	void PlantsChanged()
	{
		plantsStale = true;
		plantVersion++;
		if (plantCancel) {
			*plantCancel = true;
		}
	}
	//Plants put by hand (the plant brush of the Vegetation tab, MapImportVegetation.cpp): their own layer, in the history and the project,
	//never placed again by a biome change; drawn by placedLayer after the upload in UpdatePlants
	placedplants::Layer placedPlants;
	VegetationLayer placedLayer;
	bool placedUploadStale = true;
	int placedUploadModels = -1;		//ready models of the last upload; more loaded = upload again
	void PlacedPlantsChanged() { placedUploadStale = true; dirty = true; }
	//The plant brush: a dab where the stroke starts and then every max(spacing, radius / 2, pixel) metres of the cursor; Ctrl erases.
	//Letting the button go is one history step with the layer from before
	struct PlantStroke {
		placedplants::Layer before;
		placedplants::Brush brush;				//models, count, spacing and scale of the stroke; centre and seed set per dab
		glm::vec2 lastDab = glm::vec2(0.0f);	//metres
		bool dabbed = false;
		bool changed = false;
		bool erase = false;
	};
	std::optional<PlantStroke> plantStroke;
	bool plantBrushOn = false;
	std::vector<std::string> brushModels;	//names of the models the brush picks from
	float plantRadius = 10.0f;				//metres
	int plantBrushCount = 1;
	float plantSpacing = 2.0f;				//metres
	float plantScaleMin = 0.8f, plantScaleMax = 1.2f;
	uint32_t plantSeed = 0;
	const LightSource* lastLight = nullptr;	//of the last Draw, for the model preview of the Vegetation tab
	std::string plantBrushStatus;			//why the brush placed nothing
	//Every model of the list with its loaded variants (0 = not loaded), as placement and export name them
	std::vector<vegexport::Model> PlantModels() const;
	//World XZ (before modelScale) of the centre of texel (0, 0), where the plants of map metre (0, 0) are drawn
	glm::vec2 PlantTexelZero() const;
	//Starts, continues or ends a stroke of the plant brush (GL thread, from UpdateSculpt)
	void UpdatePlantBrush(const Camera& camera, bool active);
	void FinishPlantStroke();
	//Puts the placed plants on the GPU (standing on the current heights); from UpdatePlants when stale
	void UploadPlacedPlants();
	//The 2D map draws no plants: the placed ones are shown as dots, projected after each Draw (window points) and drawn by SculptInputImGui
	std::vector<ImVec2> placedDots;
	ImVec2 dotsClipMin, dotsClipMax;
	static constexpr size_t maxPlacedDots = 50000;	//above it every n-th plant
	void ProjectPlacedDots(Camera& camera, const LightSource& light);
	//The Vegetation tab: automatic plants, the plant brush, the models with their preview
	void VegetationImGui();
	//Number of models loaded with variants
	int ReadyModels() const;
	//What placement needs, for the plants shown and for the export (the same plants)
	vegexport::Input PlantInput() const;
	//Takes a finished placement, starts a new one when the plants are out of date; called every frame (GL thread)
	void UpdatePlants();
	//The Automatic plants section of the Vegetation tab
	void PlantsImGui();

	//"Take georeference from file...": the file chosen, its header and the part of it the map was made of
	std::filesystem::path geoFile;
	heightmap::FileHeader geoFileHeader;
	int geoCrop[2] = { 0, 0 };
	int geoDownsample = 1;
	std::string geoStatus;
	bool geoStatusError = false;

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
	int chosenEpsg = geo::degreesEpsg;	//system of a file whose coordinates name none
	std::string error;
};
