#include "VegetationCheck.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <lodepng.h>

#include "Camera.h"
#include "ChunkWorld.h"
#include "GpuHeightGenerator.h"
#include "GrassLayer.h"
#include "LightSource.h"
#include "Renderer.h"
#include "TerrainMaterials.h"
#include "TerrainView.h"
#include "TextureClass.h"
#include "VegetationLayer.h"
#include "VegetationModels.h"
#include "WorldConfig.h"

namespace {
	constexpr int screenWidth = 1920, screenHeight = 1080;
	//As TerrainGenApp: the map size of the default world, the displayed height of 1 and the camera
	constexpr int mapSize = 250;
	constexpr float heightScale = 1024.0f;
	//Render distance of the application (world units)
	constexpr float defaultRenderDistance = 5000.0f;
	//Frames measured after everything around the camera is generated and placed
	constexpr int measuredFrames = 120;

	//Blocking GL_TIME_ELAPSED around a part of a frame; fine for a benchmark, not for the application
	class Timer {
	public:
		Timer() { glGenQueries(1, &query); }
		~Timer() { glDeleteQueries(1, &query); }
		void Begin() { glBeginQuery(GL_TIME_ELAPSED, query); }
		void End() { glEndQuery(GL_TIME_ELAPSED); }
		double Ms() const
		{
			GLuint64 ns = 0;
			glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
			return static_cast<double>(ns) / 1e6;
		}
	private:
		GLuint query = 0;
	};

	struct Stats {
		double sum = 0.0, max = 0.0;
		int count = 0;
		void Add(double v) { sum += v; max = std::max(max, v); count++; }
		double Mean() const { return count ? sum / count : 0.0; }
	};

	//Map position of the middle of the largest patch of a dense forest biome near the map, found on a coarse biome map, and the
	//direction (map XZ) whose quarter of the view up to viewMetres holds the most land with plants
	//@return false if there is none
	bool FindForest(GpuHeightGenerator& gpu, const WorldConfig& world, float viewMetres, glm::vec2& position, glm::vec2& direction,
		std::string& biomeName)
	{
		constexpr int size = 256;
		constexpr float step = 32.0f;
		TextureClass height(size, size, GL_R32F), colors(size, size, GL_RGBA32F), ids(size, size, GL_R32I);
		MapArea area;
		area.step = step;
		area.origin = glm::vec2(mapSize / 2.0f) - glm::vec2(size / 2.0f * step);
		area.origin = glm::floor(area.origin / step) * step;
		area.islandSize = glm::ivec2(mapSize);
		gpu.GenerateWorld(world.terrain, world.biomes, area, height, colors, ids);
		std::vector<int> map(size * size);
		ids.Read(GL_RED_INTEGER, GL_INT, static_cast<int>(map.size() * sizeof(int)), map.data());

		//The densest default forests first
		for (const char* name : { "Forest", "Jungle", "Taiga" }) {
			int id = -1;
			for (size_t i = 0; i < world.biomes.table.size(); i++) {
				if (world.biomes.table[i].name == name) {
					id = static_cast<int>(i);
				}
			}
			if (id < 0) {
				continue;
			}
			//The sample nearest the centre with the whole square of radius r around it in the biome (r samples = r * 32 m)
			constexpr int r = 4;
			float best = INFINITY;
			for (int y = r; y < size - r; y++) {
				for (int x = r; x < size - r; x++) {
					bool inside = true;
					for (int dy = -r; dy <= r && inside; dy++) {
						for (int dx = -r; dx <= r && inside; dx++) {
							inside = map[(y + dy) * size + x + dx] == id;
						}
					}
					const float distance = glm::length(glm::vec2(x, y) - glm::vec2(size / 2.0f));
					if (inside && distance < best) {
						best = distance;
						position = area.origin + glm::vec2(x, y) * step;
					}
				}
			}
			if (best < INFINITY) {
				biomeName = name;
				//Directions every 10 degrees; samples within 45 degrees of one and the view distance count for it
				constexpr int directions = 36;
				int bestCount = -1;
				for (int d = 0; d < directions; d++) {
					const float angle = 6.2831853f * static_cast<float>(d) / directions;
					const glm::vec2 dir(std::cos(angle), std::sin(angle));
					int count = 0;
					for (int y = 0; y < size; y++) {
						for (int x = 0; x < size; x++) {
							const glm::vec2 offset = area.origin + glm::vec2(x, y) * step - position;
							const float distance = glm::length(offset);
							const int sampleId = map[y * size + x];
							if (distance < 1.0f || distance > viewMetres || glm::dot(offset / distance, dir) < 0.7071f || sampleId < 0
								|| sampleId >= static_cast<int>(world.biomes.table.size()) || world.biomes.table[sampleId].plants.empty()) {
								continue;
							}
							count++;
						}
					}
					if (count > bestCount) {
						bestCount = count;
						direction = dir;
					}
				}
				return true;
			}
		}
		return false;
	}

	//Terrain height (world units) at a map position, from one generated sample
	float HeightAt(GpuHeightGenerator& gpu, const WorldConfig& world, glm::vec2 position)
	{
		TextureClass height(1, 1, GL_R32F), colors(1, 1, GL_RGBA32F), ids(1, 1, GL_R32I);
		MapArea area;
		area.origin = glm::floor(position);
		area.islandSize = glm::ivec2(mapSize);
		gpu.GenerateWorld(world.terrain, world.biomes, area, height, colors, ids);
		float value = 0.0f;
		height.Read(GL_RED, GL_FLOAT, sizeof(float), &value);
		return value * heightScale;
	}

	//Frames rendered offscreen: the back buffer of a hidden window has undefined contents, so it cannot be read back
	struct OffscreenTarget {
		GLuint framebuffer = 0, color = 0, depth = 0;
		OffscreenTarget()
		{
			glCreateRenderbuffers(1, &color);
			glNamedRenderbufferStorage(color, GL_RGBA8, screenWidth, screenHeight);
			glCreateRenderbuffers(1, &depth);
			glNamedRenderbufferStorage(depth, GL_DEPTH_COMPONENT24, screenWidth, screenHeight);
			glCreateFramebuffers(1, &framebuffer);
			glNamedFramebufferRenderbuffer(framebuffer, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
			glNamedFramebufferRenderbuffer(framebuffer, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
		}
		~OffscreenTarget()
		{
			glDeleteFramebuffers(1, &framebuffer);
			glDeleteRenderbuffers(1, &color);
			glDeleteRenderbuffers(1, &depth);
		}
		OffscreenTarget(const OffscreenTarget&) = delete;
		OffscreenTarget& operator=(const OffscreenTarget&) = delete;
	};

	//Saves the bound framebuffer as a PNG
	bool SaveScreenshot(const std::string& path)
	{
		std::vector<unsigned char> pixels(static_cast<size_t>(screenWidth) * screenHeight * 4), flipped(pixels.size());
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glReadPixels(0, 0, screenWidth, screenHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		const size_t row = static_cast<size_t>(screenWidth) * 4;
		for (int y = 0; y < screenHeight; y++) {
			std::copy_n(&pixels[static_cast<size_t>(screenHeight - 1 - y) * row], row, &flipped[static_cast<size_t>(y) * row]);
		}
		for (size_t i = 3; i < flipped.size(); i += 4) {
			flipped[i] = 255;
		}
		return lodepng::encode(path, flipped, screenWidth, screenHeight) == 0;
	}

	//Runs the frames of the infinite world with vegetation until everything in range is generated and placed, then measures
	int Bench(float density, float eyeMetres, const std::string& screenshot, float viewMetres)
	{
		WorldConfig world;
		world.width = world.height = mapSize;
		GpuHeightGenerator gpu;
		ChunkWorld chunks;
		VegetationModels models;
		if (!gpu.Initialize() || !chunks.Initialize() || !models.Initialize("res/vegetation")) {
			std::cout << "[ERROR] Initialization failed" << std::endl;
			return -1;
		}
		while (models.LoadingCount() > 0) {
			models.Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		models.Update();

		const float renderDistance = viewMetres > 0.0f ? viewMetres * vegetation::unitsPerMetre : defaultRenderDistance;
		glm::vec2 forest, direction(1.0f, 0.0f);
		std::string biomeName;
		if (!FindForest(gpu, world, renderDistance / vegetation::unitsPerMetre, forest, direction, biomeName)) {
			std::cout << "[ERROR] No forest near the map" << std::endl;
			return -1;
		}
		chunks.SetBiomes(true);
		chunks.AlignWithMap(world.width, world.height);
		//Camera eyeMetres above the forest, looking at the ground 300 m away towards the most land with plants
		const glm::vec2 xz = chunks.GetMapOrigin() + forest * TerrainView::worldUnitsPerTexel;
		const float ground = HeightAt(gpu, world, forest);
		Camera camera(screenWidth, screenHeight, glm::vec3(0.0f), 100.0f, renderDistance);
		const glm::vec3 eye(xz.x, ground + eyeMetres * vegetation::unitsPerMetre, xz.y);
		const float ahead = 300.0f * vegetation::unitsPerMetre;
		camera.LookAt(eye, eye + glm::vec3(direction.x * ahead, -eyeMetres * vegetation::unitsPerMetre, direction.y * ahead));
		std::printf("Camera in %s at map (%.0f, %.0f), ground %.0f units, eye %.0f m, density %.2f\n", biomeName.c_str(), forest.x, forest.y, ground,
			eyeMetres, density);

		LightSource light;
		Renderer renderer;
		TerrainRenderSettings settings;
		settings.heightScale = heightScale;
		settings.displayMode = utilities::heightMapMode::BIOMES;
		VegetationLayer vegetation;
		vegetation.SetDensity(density);
		if (viewMetres > 0.0f) {
			vegetation.SetTreeRange(viewMetres);
		}
		//Grass grows by the materials of the splat map; their textures are not needed for it
		TerrainMaterials materials;
		if (!materials.Initialize("res/textures/terrain_materials.json", "res/textures/terrain")) {
			return -1;
		}
		GrassLayer grass;
		VegetationLayer::World vegetationWorld;
		vegetationWorld.biomes = &world.biomes.table;
		vegetationWorld.waterHeight = world.terrain.WaterHeight();
		vegetationWorld.heightScale = heightScale;
		vegetationWorld.seed = static_cast<uint32_t>(world.biomes.detailSeed);

		std::unique_ptr<OffscreenTarget> target;
		if (!screenshot.empty() && screenshot != "-") {
			target = std::make_unique<OffscreenTarget>();
			glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
		}
		glEnable(GL_DEPTH_TEST);
		glViewport(0, 0, screenWidth, screenHeight);
		renderer.SetPatches(4);
		Timer terrainTimer, vegetationTimer, grassTimer;
		//Measured twice on the same placement: trees far away as LOD 2 meshes, then as impostors
		struct Phase {
			const char* name;
			bool impostors;
			Stats terrainGpu, grassGpu, vegetationGpu, vegetationCpu, updateCpu;
			int blades = 0;
			int drawn = 0, perLod[4] = {};
			uint64_t triangles = 0;
		};
		Phase phases[2] = { { "meshes", false }, { "impostors", true } };
		int phase = 0;
		Stats loadingUpdate, loadingPlace;
		int settled = 0, frame = 0;
		const auto start = std::chrono::steady_clock::now();
		for (; frame < 5000 && phase < 2; frame++) {
			const bool measuring = settled >= 30;
			Phase& p = phases[phase];
			vegetation.SetImpostors(p.impostors);
			const bool lastFrame = measuring && p.terrainGpu.count + 1 == measuredFrames;
			renderer.Clear(glm::vec3(0.5f, 0.6f, 0.7f));
			const auto updateStart = std::chrono::steady_clock::now();
			if (chunks.Plan(ChunkWorld::ViewFrom(camera, settings)) > 0) {
				chunks.GenerateNext([&](const MapArea& area, TextureClass& height, TextureClass* colors, TextureClass* ids, TextureClass* splat) {
					if (colors && ids) {
						gpu.GenerateWorld(world.terrain, world.biomes, area, height, *colors, *ids, splat);
					}
					else {
						gpu.GenerateTerrain(world.terrain, area, height, &world.biomes);
					}
				});
			}
			const auto placeStart = std::chrono::steady_clock::now();
			vegetation.Update(chunks, &models, vegetationWorld);
			const double placeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - placeStart).count();
			const double updateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count();

			terrainTimer.Begin();
			chunks.Draw(renderer, camera, light, settings);
			terrainTimer.End();
			grassTimer.Begin();
			grass.Draw(chunks, camera, light, settings, materials);
			grassTimer.End();
			vegetationTimer.Begin();
			const auto drawStart = std::chrono::steady_clock::now();
			vegetation.Draw(camera, light, settings, &models);
			const double drawMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - drawStart).count();
			vegetationTimer.End();
			const double terrainMs = terrainTimer.Ms(), grassMs = grassTimer.Ms(), vegetationMs = vegetationTimer.Ms();
			if (lastFrame && target) {
				const std::string file = screenshot + "_" + p.name + ".png";
				if (!SaveScreenshot(file)) {
					std::cout << "[ERROR] cannot write " << file << std::endl;
				}
			}
			glfwSwapBuffers(glfwGetCurrentContext());

			if (measuring) {
				p.terrainGpu.Add(terrainMs);
				p.grassGpu.Add(grassMs);
				p.vegetationGpu.Add(vegetationMs);
				p.vegetationCpu.Add(drawMs);
				p.updateCpu.Add(updateMs);
				if (lastFrame) {
					p.drawn = vegetation.DrawnInstances();
					for (int l = 0; l < 4; l++) {
						p.perLod[l] = vegetation.DrawnInLod(l);
					}
					p.triangles = vegetation.DrawnTriangles();
					p.blades = grass.DrawnBlades();
					phase++;
				}
			}
			else {
				const bool done = chunks.GetPendingCount() == 0 && vegetation.UnplacedCount() == 0;
				settled = done ? settled + 1 : 0;
				loadingUpdate.Add(updateMs);
				loadingPlace.Add(placeMs);
			}
		}
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		if (phase < 2) {
			std::cout << "[ERROR] The world did not settle in " << frame << " frames" << std::endl;
			return -1;
		}
		std::printf("Settled and measured in %d frames (%.1f s); placed %zu instances\n", frame, seconds, vegetation.PlacedCount());
		std::printf("While loading: update CPU (chunks, place) mean %.2f ms, max %.2f ms; of it placement mean %.2f ms, max %.2f ms\n",
			loadingUpdate.Mean(), loadingUpdate.max, loadingPlace.Mean(), loadingPlace.max);
		for (const Phase& p : phases) {
			std::printf("\nFar trees as %s: drawn %d (LOD 0/1/2: %d / %d / %d, impostors %d), %.2f M triangles\n", p.name, p.drawn,
				p.perLod[0], p.perLod[1], p.perLod[2], p.perLod[VegetationLayer::impostorLod], static_cast<double>(p.triangles) / 1e6);
			std::printf("%-28s %8s %8s\n", "per frame (ms)", "mean", "max");
			std::printf("%-28s %8.2f %8.2f\n", "terrain GPU", p.terrainGpu.Mean(), p.terrainGpu.max);
			std::printf("%-28s %8.2f %8.2f   (%d blades)\n", "grass GPU", p.grassGpu.Mean(), p.grassGpu.max, p.blades);
			std::printf("%-28s %8.2f %8.2f\n", "vegetation GPU", p.vegetationGpu.Mean(), p.vegetationGpu.max);
			std::printf("%-28s %8.2f %8.2f\n", "vegetation draw CPU", p.vegetationCpu.Mean(), p.vegetationCpu.max);
			std::printf("%-28s %8.2f %8.2f\n", "update CPU (chunks, place)", p.updateCpu.Mean(), p.updateCpu.max);
		}
		return 0;
	}
}

int RunVegetationBench(float density, float eyeMetres, const std::string& screenshot, float viewMetres)
{
	if (!glfwInit()) {
		std::cout << "[ERROR] Failed to initialize GLFW" << std::endl;
		return -1;
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	GLFWwindow* window = glfwCreateWindow(screenWidth, screenHeight, "Vegetation bench", nullptr, nullptr);
	if (!window) {
		std::cout << "[ERROR] OpenGL 4.5 is not available" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(0);
	int result = -1;
	if (glewInit() != GLEW_OK) {
		std::cout << "[ERROR] Failed to initialize GLEW" << std::endl;
	}
	else {
		//The GL objects go before the context does
		result = Bench(density, eyeMetres, screenshot, viewMetres);
	}
	glfwDestroyWindow(window);
	glfwTerminate();
	return result;
}
