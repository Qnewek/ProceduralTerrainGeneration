#pragma once
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "ChunkWorld.h"
#include "GpuErosion.h"
#include "GpuHeightGenerator.h"
#include "Camera.h"
#include "utilities.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "PresetPanel.h"
#include "ExportPanel.h"
#include "MapProject.h"
#include "PlacesPanel.h"
#include "PackPicker.h"
#include "PanelTabs.h"
#include "MapLabels.h"

//What has to be done at the next Update, all on the GPU: the noise map (with infinite generation: the chunks), then erosion of the finite map
struct NoiseRequest {
	bool noise = false;
	bool resetErosion = false;	//the next erosion starts from the noise map instead of accumulating
	bool erode = false;

	bool Any() const { return noise || resetErosion || erode; }
};

class NoiseBasedGenerationSys
{
	private:
		//Map size, noise and erosion parameters edited by the UI; generators only read them
		NoiseMapConfig config;

		bool instantUpdate = true, infiniteGeneration = true;
		double lastGenerationMs = 0.0;

		//Rendering; the finite noise map lives in the TERRAIN layer of the view, the infinite one in chunks
		TerrainView view;
		ChunkWorld chunks;
		TerrainRenderSettings renderSettings;
		GpuHeightGenerator gpu;
		//infiniteGeneration as of the last Update
		bool chunksShown = false;
		//The noise changed while the chunks were shown, so the finite map needs regenerating
		bool finiteOutdated = false;
		//The finite map is a part of the infinite world: world map position of its texel (0, 0); leaving the infinite world puts it
		//under the camera (as in TerrainGenerationSys)
		glm::ivec2 mapOffset = glm::ivec2(0);
		//The Display tab asked to centre the map on the camera (it has no camera)
		bool centerOnCamera = false;
		//Moves the finite map so its texel (0, 0) is world map position offset; new noise if that changes
		void MoveMap(glm::ivec2 offset);
		//Offset putting the middle of the map under the camera
		glm::ivec2 OffsetUnder(const Camera& camera) const;
		//Map offset of the current view: under the camera in the infinite world (where leaving it puts the map), else of the finite map
		glm::ivec2 PlaceOffset(const Camera& camera) const { return infiniteGeneration ? OffsetUnder(camera) : mapOffset; }
		//Moves the map to offset and the camera above it (a place without a camera: preset, pack)
		void GoToMap(glm::ivec2 offset, Camera& camera);

		//Every erosion method keeps its own eroded map, so both can be shown next to the noise map at once:
		//droplets in the OVERLAY layer of view, pipes in the TERRAIN layer of pipesView (a second view, as TerrainView has one free height layer)
		struct ErodedMap {
			bool shown = false;			//has a result since the last reset
			bool fromBase = true;		//the next erosion starts from the noise map instead of accumulating on this one
			double lastMs = 0.0;		//GPU time of the last erosion
		};
		static constexpr int methodCount = 2;
		ErodedMap eroded[methodCount];
		TerrainView pipesView;
		GpuErosion erosion;
		//Both methods erode at once and their maps are shown side by side (state of the editor, not of the config)
		bool compareMethods = false;
		//Seed of the next erosion run, so that runs accumulating on one map use different droplets
		uint32_t erosionRuns = 0;
		NoiseRequest pending;

		//Finite map: a reset drops the eroded maps, an erosion request erodes them (or the noise map after a reset)
		void UpdateErosion();
		//Texture holding the eroded map of a method; nullptr before the first erosion with it
		const TextureClass* ErodedTexture(erosion::Method method) const;
		//Methods whose maps are shown next to the noise map, in the order they are drawn
		std::vector<erosion::Method> ShownMethods() const;

		//Names of the maps of the last Draw
		MapLabels labels;

		//New noise map; the erosion shown next to it is dropped and starts from the new map
		void RequestNoise();
		//Finite map: generates a requested noise map
		void GenerateFinite();
		//Infinite generation: a noise request regenerates the chunks; generates the next chunks around the camera
		void UpdateChunks(const Camera& camera);
		void RequestErosion();

		//Replaces the parameters and regenerates the map; an erosion waiting to start is dropped
		void ApplyConfig(const NoiseMapConfig& loaded);
		PresetPanel presets{ "presets/noise" };
		ExportPanel exporter{ "exports" };
		std::optional<mapproject::Project> sentMap;	//"Send map" pressed, waiting for TakeSentMap
		PlacesPanel places{ "places/noise.json" };
		PackPicker packs{ "exports" };
		//Mode name in export packs
		static constexpr const char* packMode = "Noise heightmap";
		//Presets are saved with the map position and loading one with a position moves the map there
		bool presetPosition = true;
		void LoadPack(const exportpack::PackSource& source, Camera& camera);

		//Tabs of the right panel and their content
		PanelTabs generationTabs;
		void NoiseTabImGui();
		void ErosionImGui();
	public:
		NoiseBasedGenerationSys();
		~NoiseBasedGenerationSys();

		bool Initialize(int _width, int _height, float _heightScale);
		//Called every frame before Draw: generates a requested map or the chunks around the camera, then a requested erosion
		//@param camera - moved to the finite map when infinite generation is turned off
		void Update(Camera& camera);

		void Draw(Renderer& renderer, Camera& camera, LightSource& light);
		//true while the 2D map is shown instead of the 3D view
		bool FlatView() const { return renderSettings.map2d; }
		//Zoom of the 2D map for the editor camera, nullptr in 3D; the infinite world only zooms inside the area it shows
		cameramath::FlatZoom* FlatViewZoom()
		{
			renderSettings.flatZoom.fixedFrame = chunksShown;
			return renderSettings.map2d ? &renderSettings.flatZoom : nullptr;
		}
		//Infinite generation shows chunks with part of the view not generated yet
		bool HoleInView() const { return chunksShown && chunks.HasHoleInView(); }
		void ReloadShaders() { view.ReloadShader(); pipesView.ReloadShader(); chunks.ReloadShader(); gpu.ReloadShaders(); erosion.ReloadShaders(); }
		//Names of the maps shown side by side over the viewport; call inside the ImGui frame, after Draw
		void ImGuiMapLabels();
		//Right panel: the generation tabs (noise, erosion)
		void ImGuiRightPanel();
		//Content of the Display, Presets and Saving tabs of the left panel
		void ImGuiDisplay();
		//@param camera - saved with a place, moved by loading one
		void ImGuiPresets(Camera& camera);
		void ImGuiSaving();
		//A map sent to the Imported map mode from the Saving tab, once
		std::optional<mapproject::Project> TakeSentMap() { return std::exchange(sentMap, std::nullopt); }
		void ImGuiOutput();
};

