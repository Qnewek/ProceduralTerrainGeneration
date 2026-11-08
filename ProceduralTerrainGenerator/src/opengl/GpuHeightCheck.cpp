#include "GpuHeightCheck.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "BiomeGenerator.h"
#include "GpuHeightGenerator.h"
#include "GpuSurfacePass.h"
#include "SurfaceDerivatives.h"
#include "TerrainGenerator.h"

namespace {
	struct Difference {
		float max = 0.0f;
		int x = 0, y = 0;
		bool invalid = false;	//NaN or infinity in either map
		int aboveTolerance = 0;
	};

	struct CheckCase {
		std::string name;
		std::function<bool(HeightField&)> cpu;
		std::function<void(TextureClass&)> gpu;
		float tolerance;
	};

	//Rounding differs slightly between CPU and GPU, so the maps are never identical.
	//The larger tolerances match how much the CPU result itself changes when a coordinate moves by the smallest float step
	constexpr float noiseTolerance = 1e-4f;
	//At very high frequencies float coordinates are coarse (measured on CPU: 2.4e-3 for the 8 octave case below)
	constexpr float highFrequencyTolerance = 3e-3f;
	//The 4D noise jumps slightly at simplex borders: its corner radius is 0.6, not 0.5 (measured on CPU: 3.8e-4)
	constexpr float symmetricalTolerance = 5e-4f;
	//A spline table cannot follow a corner of a linear spline exactly
	constexpr float splineTolerance = 2e-3f;

	Difference Compare(const HeightField& cpu, const HeightField& gpu, float tolerance)
	{
		Difference result;
		for (int y = 0; y < cpu.GetHeight(); y++) {
			for (int x = 0; x < cpu.GetWidth(); x++) {
				const float a = cpu.At(x, y), b = gpu.At(x, y);
				if (!std::isfinite(a) || !std::isfinite(b)) {
					result.invalid = true;
				}
				const float difference = std::abs(a - b);
				if (difference > tolerance) {
					result.aboveTolerance++;
				}
				if (difference > result.max) {
					result.max = difference;
					result.x = x;
					result.y = y;
				}
			}
		}
		return result;
	}

	//Runs fn with the console silenced, because the generators log every call
	template<typename F>
	auto Muted(F fn)
	{
		std::ostringstream sink;
		std::streambuf* original = std::cout.rdbuf(sink.rdbuf());
		auto result = fn();
		std::cout.rdbuf(original);
		return result;
	}

	bool CpuNoise(const noise::NoiseConfigParameters& config, float originx, float originy, HeightField& out)
	{
		return Muted([&] {
			noise::SimplexNoiseClass generator;
			if (!generator.Initialize(out.GetWidth(), out.GetHeight()) || !generator.GenerateFractalNoise(config, originx, originy)) {
				return false;
			}
			out = generator.GetField();
			return true;
		});
	}

	bool CpuTerrain(const TerrainConfig& config, float originx, float originy, HeightField& out)
	{
		return Muted([&] {
			TerrainGenerator generator;
			if (!generator.Initialize(out.GetWidth(), out.GetHeight()) || !generator.GenerateTerrain(config, originx, originy)) {
				return false;
			}
			out = generator.GetHeightField();
			return true;
		});
	}

	std::vector<CheckCase> BuildCases(GpuHeightGenerator& gpu)
	{
		std::vector<CheckCase> cases;
		const auto addNoise = [&](const std::string& name, const noise::NoiseConfigParameters& config, float tolerance = noiseTolerance, float originx = 0.0f, float originy = 0.0f) {
			cases.push_back({ "noise: " + name,
				[=](HeightField& out) { return CpuNoise(config, originx, originy, out); },
				[=, &gpu](TextureClass& target) { gpu.GenerateNoise(config, MapArea{ glm::vec2(originx, originy) }, target); },
				tolerance });
		};
		const auto addTerrain = [&](const std::string& name, const TerrainConfig& config, float tolerance, float originx = 0.0f, float originy = 0.0f) {
			cases.push_back({ "terrain: " + name,
				[=](HeightField& out) { return CpuTerrain(config, originx, originy, out); },
				[=, &gpu](TextureClass& target) { gpu.GenerateTerrain(config, MapArea{ glm::vec2(originx, originy) }, target); },
				tolerance });
		};

		noise::NoiseConfigParameters n;
		addNoise("default", n);
		addNoise("origin", n, noiseTolerance, 123.5f, -77.25f);

		n = {}; n.seed = 98765; n.xoffset = -3.5f; n.yoffset = 12.25f;
		addNoise("seed and offset", n);
		n = {}; n.lacunarity = 3.1f; n.persistance = 0.8f; n.scale = 2.7f; n.constrast = 1.7f;
		//Fewer octaves keep the strict tolerance, so a real error cannot hide behind the larger one
		for (int octaves : { 1, 4, 8 }) {
			n.octaves = octaves;
			addNoise("fractal parameters, " + std::to_string(octaves) + " oct", n, octaves < 8 ? noiseTolerance : highFrequencyTolerance);
		}

		for (auto [option, name] : { std::pair{ noise::Options::REFIT_ALL, "refit" }, { noise::Options::FLATTEN_NEGATIVES, "flatten" },
			{ noise::Options::REVERT_NEGATIVES, "revert" }, { noise::Options::NOTHING, "nothing" } }) {
			n = {}; n.option = option;
			addNoise(std::string("option ") + name, n);
		}

		n = {}; n.Ridge = true; n.RidgeGain = 3.0f; n.RidgeOffset = 0.8f; n.option = noise::Options::NOTHING;
		addNoise("ridge", n);
		n = {}; n.redistribution = 2.5f; n.option = noise::Options::NOTHING;
		addNoise("redistribution 2.5", n);
		n = {}; n.redistribution = 0.5f; n.option = noise::Options::NOTHING;
		addNoise("redistribution 0.5", n);
		n = {}; n.symmetrical = true;
		for (int octaves : { 1, 8 }) {
			n.octaves = octaves;
			addNoise("symmetrical, " + std::to_string(octaves) + " oct", n, octaves == 1 ? noiseTolerance : symmetricalTolerance);
		}

		for (auto [type, name] : { std::pair{ noise::IslandType::CONE, "cone" }, { noise::IslandType::DIAGONAL, "diagonal" },
			{ noise::IslandType::EUCLIDEAN_SQUARED, "euclidean" }, { noise::IslandType::SQUARE_BUMP, "square bump" },
			{ noise::IslandType::HYPERBOLOID, "hyperboloid" }, { noise::IslandType::SQUIRCLE, "squircle" }, { noise::IslandType::TRIG, "trig" } }) {
			n = {}; n.island = true; n.islandType = type; n.mixPower = 0.6f;
			addNoise(std::string("island ") + name, n);
		}

		TerrainConfig t = TerrainConfig::Default();
		addTerrain("linear", t, noiseTolerance);
		addTerrain("linear with origin", t, noiseTolerance, 40.0f, 900.5f);
		t.method = EvaluationMethod::SPLINE_COMBINE;
		addTerrain("linear splines", t, splineTolerance);
		t.continentalnessSpline.cubic = t.mountainousnessSpline.cubic = t.weirdnessSpline.cubic = true;
		addTerrain("cubic splines", t, splineTolerance);
		t.method = EvaluationMethod::C;
		addTerrain("no method", t, noiseTolerance);
		return cases;
	}

	//A cell lying right at a level boundary can land on the other side of it, because the noises differ slightly
	constexpr double biomeMismatchTolerance = 0.001;

	bool CheckBiomes(GpuHeightGenerator& gpu, const std::string& name, const TerrainConfig& terrain, const BiomeConfig& biomes, int width, int height)
	{
		BiomeMap cpuMap = Muted([&] {
			TerrainGenerator terrainGenerator;
			BiomeGenerator biomeGenerator;
			BiomeMap map;
			if (terrainGenerator.Initialize(width, height) && biomeGenerator.Initialize(width, height) && terrainGenerator.GenerateNoises(terrain) && biomeGenerator.GenerateComponentNoises(biomes)
				&& biomeGenerator.Biomify(biomes, terrainGenerator.GetSelectedNoise(WorldGenParameter::CONTINENTALNESS), terrainGenerator.GetSelectedNoise(WorldGenParameter::MOUNTAINOUSNESS),
					terrainGenerator.GetSelectedNoise(WorldGenParameter::WEIRDNESS))) {
				map = biomeGenerator.GetBiomeMap();
			}
			return map;
		});
		if (cpuMap.Empty()) {
			std::cout << name << ": [ERROR] CPU generation failed\n";
			return false;
		}

		TextureClass colors(width, height, GL_RGBA32F), ids(width, height, GL_R32I);
		gpu.GenerateBiomes(biomes, terrain, biome::DefaultBiomes(), MapArea{}, colors, ids);
		BiomeMap gpuMap(width, height);
		ids.Read(GL_RED_INTEGER, GL_INT, static_cast<int>(gpuMap.Size() * sizeof(int)), gpuMap.Data());

		//Every color must belong to the id next to it
		std::vector<float> colorData(gpuMap.Size() * 4);
		colors.Read(GL_RGBA, GL_FLOAT, static_cast<int>(colorData.size() * sizeof(float)), colorData.data());
		const std::vector<biome::Biome> table = biome::DefaultBiomes();
		size_t different = 0, wrongColor = 0;
		for (size_t i = 0; i < gpuMap.Size(); i++) {
			different += cpuMap[i] != gpuMap[i] ? 1 : 0;
			const glm::vec3 expected = table[gpuMap[i]].GetColor();
			wrongColor += expected.r != colorData[i * 4] || expected.g != colorData[i * 4 + 1] || expected.b != colorData[i * 4 + 2] ? 1 : 0;
		}

		const double share = static_cast<double>(different) / static_cast<double>(gpuMap.Size());
		const bool ok = share <= biomeMismatchTolerance && wrongColor == 0;
		std::cout << std::left << std::setw(37) << name << different << " of " << gpuMap.Size() << " cells differ, wrong colors: " << wrongColor << "   " << (ok ? "ok" : "TOO DIFFERENT") << "\n";
		return ok;
	}

	//Both sides read the same height map, so only the rounding of a few operations differs (e.g. a fused multiply-add on the GPU).
	//Differences of nearly equal heights lose the relative precision, so the error is measured in height units,
	//in steps of float at the largest height of the map
	constexpr float surfaceUlpTolerance = 4.0f;
	//Same distance between texels as in TerrainView; a power of two, so dividing by it rounds nothing
	constexpr float surfaceTexelSize = 4.0f;

	//Compares the surface map of the GPU with surface::Compute on the same input
	//@param heightScale - multiplies the input, to test steep maps without a new generator
	bool CheckSurface(GpuSurfacePass& pass, const std::string& name, HeightField height, float heightScale)
	{
		for (size_t i = 0; i < height.Size(); i++) {
			height[i] *= heightScale;
		}
		const int width = height.GetWidth(), rows = height.GetHeight();
		TextureClass input(width, rows, GL_R32F), output(width, rows, GL_RGBA32F);
		input.Upload(height.Data(), GL_RED, GL_FLOAT);
		pass.Run(input, surfaceTexelSize, output);
		surface::DerivativeMap gpuMap(width, rows);
		output.Read(GL_RGBA, GL_FLOAT, static_cast<int>(gpuMap.Size() * sizeof(surface::Derivatives)), gpuMap.Data());
		const surface::DerivativeMap cpuMap = surface::Compute(height, surfaceTexelSize);

		float highest = 0.0f;
		for (size_t i = 0; i < height.Size(); i++) {
			highest = std::max(highest, std::abs(height[i]));
		}
		const float ulp = std::max(highest, 1e-30f) * std::numeric_limits<float>::epsilon();

		//Gradient times the texel size and Laplacian times its square are height differences
		bool ok = true;
		std::cout << std::left << std::setw(37) << name;
		const char* channels[] = { "dx", "dy", "laplacian" };
		for (int c = 0; c < 3; c++) {
			const auto value = [c](const surface::Derivatives& d) {
				return c == 0 ? d.dx * surfaceTexelSize : c == 1 ? d.dy * surfaceTexelSize : d.laplacian * surfaceTexelSize * surfaceTexelSize;
			};
			float difference = 0.0f;
			bool invalid = false;
			for (size_t i = 0; i < cpuMap.Size(); i++) {
				const float a = value(cpuMap[i]), b = value(gpuMap[i]);
				invalid = invalid || !std::isfinite(a) || !std::isfinite(b);
				difference = std::max(difference, std::abs(a - b));
			}
			const float ulps = difference / ulp;
			const bool channelOk = !invalid && ulps <= surfaceUlpTolerance;
			ok = ok && channelOk;
			std::cout << channels[c] << " " << std::fixed << std::setprecision(1) << ulps << " ulp" << std::defaultfloat << (channelOk ? "" : invalid ? " INVALID" : " TOO DIFFERENT") << "   ";
		}
		std::cout << (ok ? "ok" : "") << "\n";
		return ok;
	}

	//A chunk is a part of the map generated on its own, with an origin and the island size of the whole map; a coarser
	//level of detail also takes every step-th sample. It has to equal the same samples of the whole map exactly,
	//or chunk borders would show seams and levels would not meet
	bool CheckChunks(GpuHeightGenerator& gpu, int width, int height)
	{
		constexpr int partWidth = 131, partHeight = 50;
		const glm::ivec2 corner(300, 30);
		bool allOk = true;
		const auto check = [&](const std::string& name, GLenum format, GLenum type, const std::function<void(const MapArea&, TextureClass&)>& generate) {
			constexpr int bytes = 4;
			const GLenum internalFormat = format == GL_RED_INTEGER ? GL_R32I : GL_R32F;
			TextureClass whole(width, height, internalFormat);
			generate(MapArea{}, whole);
			std::vector<unsigned char> wholeData(static_cast<size_t>(width) * height * bytes), partData(static_cast<size_t>(partWidth) * partHeight * bytes);
			whole.Read(format, type, static_cast<int>(wholeData.size()), wholeData.data());
			for (int step : { 1, 2, 4 }) {
				TextureClass part(partWidth, partHeight, internalFormat);
				generate(MapArea{ glm::vec2(corner), static_cast<float>(step), glm::ivec2(width, height) }, part);
				part.Read(format, type, static_cast<int>(partData.size()), partData.data());
				int different = 0;
				for (int y = 0; y < partHeight; y++) {
					for (int x = 0; x < partWidth; x++) {
						const unsigned char* a = &wholeData[(static_cast<size_t>(corner.y + y * step) * width + corner.x + x * step) * bytes];
						const unsigned char* b = &partData[(static_cast<size_t>(y) * partWidth + x) * bytes];
						different += std::memcmp(a, b, bytes) != 0 ? 1 : 0;
					}
				}
				const bool ok = different == 0;
				allOk = allOk && ok;
				std::cout << std::left << std::setw(37) << (name + ", step " + std::to_string(step)) << different << " of " << partWidth * partHeight << " texels differ   " << (ok ? "ok" : "SEAMS") << "\n";
			}
		};

		noise::NoiseConfigParameters n;
		n.island = true;
		n.mixPower = 0.6f;
		check("chunk: noise with island", GL_RED, GL_FLOAT, [&](const MapArea& area, TextureClass& t) { gpu.GenerateNoise(n, area, t); });
		TerrainConfig terrain = TerrainConfig::Default();
		terrain.method = EvaluationMethod::SPLINE_COMBINE;
		check("chunk: terrain", GL_RED, GL_FLOAT, [&](const MapArea& area, TextureClass& t) { gpu.GenerateTerrain(terrain, area, t); });
		const BiomeConfig biomes = BiomeConfig::Default();
		const std::vector<biome::Biome> table = biome::DefaultBiomes();
		check("chunk: biome ids", GL_RED_INTEGER, GL_INT, [&](const MapArea& area, TextureClass& ids) {
			TextureClass colors(ids.GetWidth(), ids.GetHeight(), GL_RGBA32F);
			gpu.GenerateBiomes(biomes, terrain, table, area, colors, ids);
		});
		return allOk;
	}

	bool RunComparison(GpuHeightGenerator& gpu, GpuSurfacePass& surfacePass)
	{
		//Not square, so swapped axes are detected; larger than one part of a dispatch along X
		constexpr int width = 1100, height = 257;
		TextureClass target(width, height, GL_R32F);
		HeightField cpuMap(width, height), gpuMap(width, height);

		bool allOk = true;
		std::cout << "case                                 max difference   at          result\n";
		for (const CheckCase& c : BuildCases(gpu)) {
			if (!c.cpu(cpuMap)) {
				std::cout << c.name << ": [ERROR] CPU generation failed\n";
				allOk = false;
				continue;
			}
			c.gpu(target);
			target.Read(GL_RED, GL_FLOAT, static_cast<int>(gpuMap.Size() * sizeof(float)), gpuMap.Data());

			const Difference difference = Compare(cpuMap, gpuMap, c.tolerance);
			const bool ok = !difference.invalid && difference.max <= c.tolerance;
			allOk = allOk && ok;
			std::cout << std::left << std::setw(37) << c.name << std::scientific << std::setprecision(2) << std::setw(17) << difference.max
				<< std::setw(12) << (std::to_string(difference.x) + "," + std::to_string(difference.y))
				<< (ok ? "ok" : (difference.invalid ? "INVALID VALUES" : "TOO DIFFERENT")) << std::defaultfloat
				<< (difference.aboveTolerance > 0 ? " (" + std::to_string(difference.aboveTolerance) + " px)" : "") << "\n";
		}

		const TerrainConfig terrain = TerrainConfig::Default();
		BiomeConfig biomes = BiomeConfig::Default();
		allOk = CheckBiomes(gpu, "biomes: default", terrain, biomes, width, height) && allOk;
		biomes.temperature.seed = 777;
		biomes.humidityLevels = { -1.0f, -0.2f, 0.1f, 0.4f, 1.01f };
		biomes.continentalnessLevels = { -1.0f, -0.4f, -0.1f, 0.2f, 0.5f, 1.01f };
		allOk = CheckBiomes(gpu, "biomes: other seed and levels", terrain, biomes, width, height) && allOk;

		std::cout << "\n";
		allOk = CheckChunks(gpu, width, height) && allOk;

		std::cout << "\nsurface map: largest difference in height units, in steps of float at the largest height\n";
		HeightField input(width, height);
		if (CpuTerrain(terrain, 0.0f, 0.0f, input)) {
			allOk = CheckSurface(surfacePass, "surface: default terrain", input, 1.0f) && allOk;
			allOk = CheckSurface(surfacePass, "surface: default terrain x 256", input, 256.0f) && allOk;
		}
		noise::NoiseConfigParameters rough;
		rough.octaves = 8;
		rough.lacunarity = 3.1f;
		rough.scale = 2.7f;
		if (CpuNoise(rough, 0.0f, 0.0f, input)) {
			allOk = CheckSurface(surfacePass, "surface: 8 octave noise", input, 1.0f) && allOk;
		}
		return allOk;
	}

	//@return median time of finishing the work given by fn, in ms. The first run is skipped: drivers finish compiling shaders then
	template<typename F>
	double MedianMs(F fn, int runs)
	{
		fn();
		std::vector<double> times;
		for (int i = 0; i < runs; i++) {
			auto start = std::chrono::steady_clock::now();
			fn();
			times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
		}
		std::sort(times.begin(), times.end());
		return times[times.size() / 2];
	}

	void RunTimings(GpuHeightGenerator& gpu, GpuSurfacePass& surfacePass)
	{
		const TerrainConfig config = TerrainConfig::Default();
		std::cout << "\nterrain, default config   CPU [ms]   GPU [ms]\n";
		for (int size : { 512, 1024, 2048, 4096 }) {
			TextureClass target(size, size, GL_R32F);
			const double gpuMs = MedianMs([&] {
				gpu.GenerateTerrain(config, MapArea{}, target);
				glFinish();
			}, 9);
			HeightField cpuMap(size, size);
			const double cpuMs = MedianMs([&] { CpuTerrain(config, 0.0f, 0.0f, cpuMap); }, 3);
			std::cout << std::left << std::setw(26) << (std::to_string(size) + "x" + std::to_string(size)) << std::right << std::fixed << std::setprecision(2)
				<< std::setw(8) << cpuMs << std::setw(11) << gpuMs << std::defaultfloat << "\n" << std::left;
		}

		std::cout << "\nsurface map               CPU [ms]   GPU [ms]\n";
		for (int size : { 512, 1024, 2048, 4096 }) {
			TextureClass height(size, size, GL_R32F), target(size, size, GL_RGBA32F);
			gpu.GenerateTerrain(config, MapArea{}, height);
			const double gpuMs = MedianMs([&] {
				surfacePass.Run(height, surfaceTexelSize, target);
				glFinish();
			}, 9);
			HeightField cpuHeight(size, size);
			height.Read(GL_RED, GL_FLOAT, static_cast<int>(cpuHeight.Size() * sizeof(float)), cpuHeight.Data());
			const double cpuMs = MedianMs([&] { surface::Compute(cpuHeight, surfaceTexelSize); }, 3);
			std::cout << std::left << std::setw(26) << (std::to_string(size) + "x" + std::to_string(size)) << std::right << std::fixed << std::setprecision(2)
				<< std::setw(8) << cpuMs << std::setw(11) << gpuMs << std::defaultfloat << "\n" << std::left;
		}
	}
}

int RunGpuHeightCheck()
{
	if (!glfwInit()) {
		std::cout << "[ERROR] Failed to initialize GLFW" << std::endl;
		return -1;
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	GLFWwindow* window = glfwCreateWindow(64, 64, "GPU check", nullptr, nullptr);
	if (!window) {
		std::cout << "[ERROR] OpenGL 4.5 is not available" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	int result = -1;
	if (glewInit() != GLEW_OK) {
		std::cout << "[ERROR] Failed to initialize GLEW" << std::endl;
	}
	else {
		std::cout << "GPU: " << glGetString(GL_RENDERER) << "\n\n";
		//The GL objects must go before the context does
		GpuHeightGenerator gpu;
		GpuSurfacePass surfacePass;
		if (gpu.Initialize() && surfacePass.Initialize()) {
			const bool ok = RunComparison(gpu, surfacePass);
			RunTimings(gpu, surfacePass);
			std::cout << "\n" << (ok ? "All maps match" : "[ERROR] Some maps differ") << std::endl;
			result = ok ? 0 : 1;
		}
	}
	glfwDestroyWindow(window);
	glfwTerminate();
	return result;
}
