#include "GpuHeightCheck.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Grid.h"
#include "GpuHeightGenerator.h"
#include "GpuSurfacePass.h"

namespace {
	//Not square, so swapped axes are detected; larger than one part of a dispatch along X
	constexpr int mapWidth = 1100, mapHeight = 257;
	//Same distance between texels as in TerrainView; a power of two, so dividing by it rounds nothing
	constexpr float surfaceTexelSize = 4.0f;

	//One line of the report, padded so that results form a column
	void Report(const std::string& name, const std::string& details, bool ok)
	{
		std::cout << std::left << std::setw(40) << name << std::setw(44) << details << (ok ? "ok" : "FAILED") << "\n";
	}

	HeightField ReadHeight(const TextureClass& texture)
	{
		HeightField map(texture.GetWidth(), texture.GetHeight());
		texture.Read(GL_RED, GL_FLOAT, static_cast<int>(map.Size() * sizeof(float)), map.Data());
		return map;
	}

	struct RangeCase {
		std::string name;
		std::function<void(TextureClass&)> generate;
		float min, max;
	};

	//Every value must be finite and within the range the configuration promises
	bool CheckRanges(GpuHeightGenerator& gpu)
	{
		std::vector<RangeCase> cases;
		const auto addNoise = [&](const std::string& name, const noise::NoiseConfigParameters& config, float min, float max) {
			cases.push_back({ "noise: " + name, [=, &gpu](TextureClass& t) { gpu.GenerateNoise(config, MapArea{}, t); }, min, max });
		};

		noise::NoiseConfigParameters n;
		n.octaves = 8;
		n.constrast = 1.5f;
		for (auto [option, name, min] : { std::tuple{ noise::Options::NOTHING, "nothing", -1.0f }, { noise::Options::REFIT_ALL, "refit", 0.0f },
			{ noise::Options::FLATTEN_NEGATIVES, "flatten", 0.0f }, { noise::Options::REVERT_NEGATIVES, "revert", 0.0f } }) {
			n.option = option;
			addNoise(std::string("option ") + name, n, min, 1.0f);
		}
		n = {}; n.Ridge = true; n.option = noise::Options::NOTHING;
		addNoise("ridge", n, 0.0f, 1.0f);
		n = {}; n.Ridge = true; n.redistribution = 0.5f; n.option = noise::Options::NOTHING;
		addNoise("ridge, redistribution 0.5", n, -1.0f, 1.0f);
		n = {}; n.redistribution = 2.5f; n.option = noise::Options::NOTHING;
		addNoise("redistribution 2.5", n, -1.0f, 1.0f);
		n = {}; n.symmetrical = true;
		addNoise("symmetrical", n, -1.0f, 1.0f);
		for (auto [type, name] : { std::pair{ noise::IslandType::CONE, "cone" }, { noise::IslandType::DIAGONAL, "diagonal" },
			{ noise::IslandType::EUCLIDEAN_SQUARED, "euclidean" }, { noise::IslandType::SQUARE_BUMP, "square bump" },
			{ noise::IslandType::HYPERBOLOID, "hyperboloid" }, { noise::IslandType::SQUIRCLE, "squircle" }, { noise::IslandType::TRIG, "trig" } }) {
			n = {}; n.island = true; n.islandType = type; n.mixPower = 0.6f;
			addNoise(std::string("island ") + name, n, 0.0f, 1.0f);
		}

		TerrainConfig t = TerrainConfig::Default();
		cases.push_back({ "terrain: linear", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });
		t.method = EvaluationMethod::SPLINE_COMBINE;
		cases.push_back({ "terrain: linear splines", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, 0.0f, 1.0f });
		t.continentalnessSpline.cubic = t.mountainousnessSpline.cubic = t.weirdnessSpline.cubic = true;
		//A cubic spline overshoots between its points
		cases.push_back({ "terrain: cubic splines", [=, &gpu](TextureClass& target) { gpu.GenerateTerrain(t, MapArea{}, target); }, -0.1f, 1.1f });

		std::cout << "values: finite and within the range of the configuration\n";
		TextureClass target(mapWidth, mapHeight, GL_R32F);
		bool allOk = true;
		for (const RangeCase& c : cases) {
			c.generate(target);
			const HeightField map = ReadHeight(target);
			size_t outside = 0;
			float low = map[0], high = map[0];
			for (size_t i = 0; i < map.Size(); i++) {
				outside += !(map[i] >= c.min && map[i] <= c.max) ? 1 : 0;
				low = std::min(low, map[i]);
				high = std::max(high, map[i]);
			}
			//A constant map means the configuration had no effect
			const bool ok = outside == 0 && high > low;
			allOk = allOk && ok;
			std::ostringstream details;
			details << std::fixed << std::setprecision(3) << "[" << low << ", " << high << "], " << outside << " outside";
			Report(c.name, details.str(), ok);
		}
		return allOk;
	}

	//The same configuration gives the same map, and a change of the seed changes it
	bool CheckDeterminism(GpuHeightGenerator& gpu)
	{
		TextureClass first(mapWidth, mapHeight, GL_R32F), second(mapWidth, mapHeight, GL_R32F);
		TerrainConfig config = TerrainConfig::Default();
		gpu.GenerateTerrain(config, MapArea{}, first);
		gpu.GenerateTerrain(config, MapArea{}, second);
		const HeightField a = ReadHeight(first), b = ReadHeight(second);
		const bool same = std::memcmp(a.Data(), b.Data(), a.Size() * sizeof(float)) == 0;
		Report("terrain: generated twice", same ? "identical" : "different", same);

		config.continentalness.seed += 1;
		gpu.GenerateTerrain(config, MapArea{}, second);
		const HeightField c = ReadHeight(second);
		size_t changed = 0;
		for (size_t i = 0; i < a.Size(); i++) {
			changed += a[i] != c[i] ? 1 : 0;
		}
		//Most texels change; some stay where a factor is 0 anyway
		const bool differs = changed > a.Size() / 2;
		Report("terrain: other continentalness seed", std::to_string(changed) + " of " + std::to_string(a.Size()) + " texels changed", differs);
		return same && differs;
	}

	//Ids come from the table and every color belongs to the id next to it
	bool CheckBiomes(GpuHeightGenerator& gpu)
	{
		TextureClass colors(mapWidth, mapHeight, GL_RGBA32F), ids(mapWidth, mapHeight, GL_R32I);
		const std::vector<biome::Biome> table = biome::DefaultBiomes();
		gpu.GenerateBiomes(BiomeConfig::Default(), TerrainConfig::Default(), table, MapArea{}, colors, ids);
		BiomeMap idMap(mapWidth, mapHeight);
		ids.Read(GL_RED_INTEGER, GL_INT, static_cast<int>(idMap.Size() * sizeof(int)), idMap.Data());
		std::vector<float> colorData(idMap.Size() * 4);
		colors.Read(GL_RGBA, GL_FLOAT, static_cast<int>(colorData.size() * sizeof(float)), colorData.data());

		size_t unknown = 0, wrongColor = 0;
		std::vector<size_t> counts(table.size(), 0);
		for (size_t i = 0; i < idMap.Size(); i++) {
			const auto found = std::find_if(table.begin(), table.end(), [&](const biome::Biome& b) { return b.GetId() == idMap[i]; });
			if (found == table.end()) {
				unknown++;
				continue;
			}
			counts[found - table.begin()]++;
			const glm::vec3 expected = found->GetColor();
			wrongColor += expected.r != colorData[i * 4] || expected.g != colorData[i * 4 + 1] || expected.b != colorData[i * 4 + 2] ? 1 : 0;
		}
		const size_t present = static_cast<size_t>(std::count_if(counts.begin(), counts.end(), [](size_t c) { return c > 0; }));
		//The default world has more than one biome on a map this large
		const bool ok = unknown == 0 && wrongColor == 0 && present > 1;
		Report("biomes: default", std::to_string(present) + " biomes, " + std::to_string(unknown) + " unknown ids, " + std::to_string(wrongColor) + " wrong colors", ok);
		return ok;
	}

	//A chunk is a part of the map generated on its own, with an origin and the island size of the whole map; a coarser
	//level of detail also takes every step-th sample. It has to equal the same samples of the whole map exactly,
	//or chunk borders would show seams and levels would not meet
	bool CheckChunks(GpuHeightGenerator& gpu)
	{
		constexpr int partWidth = 131, partHeight = 50;
		const glm::ivec2 corner(300, 30);
		bool allOk = true;
		const auto check = [&](const std::string& name, GLenum format, GLenum type, const std::function<void(const MapArea&, TextureClass&)>& generate) {
			constexpr int bytes = 4;
			const GLenum internalFormat = format == GL_RED_INTEGER ? GL_R32I : GL_R32F;
			TextureClass whole(mapWidth, mapHeight, internalFormat);
			generate(MapArea{}, whole);
			std::vector<unsigned char> wholeData(static_cast<size_t>(mapWidth) * mapHeight * bytes), partData(static_cast<size_t>(partWidth) * partHeight * bytes);
			whole.Read(format, type, static_cast<int>(wholeData.size()), wholeData.data());
			for (int step : { 1, 2, 4 }) {
				TextureClass part(partWidth, partHeight, internalFormat);
				generate(MapArea{ glm::vec2(corner), static_cast<float>(step), glm::ivec2(mapWidth, mapHeight) }, part);
				part.Read(format, type, static_cast<int>(partData.size()), partData.data());
				int different = 0;
				for (int y = 0; y < partHeight; y++) {
					for (int x = 0; x < partWidth; x++) {
						const unsigned char* a = &wholeData[(static_cast<size_t>(corner.y + y * step) * mapWidth + corner.x + x * step) * bytes];
						const unsigned char* b = &partData[(static_cast<size_t>(y) * partWidth + x) * bytes];
						different += std::memcmp(a, b, bytes) != 0 ? 1 : 0;
					}
				}
				const bool ok = different == 0;
				allOk = allOk && ok;
				Report(name + ", step " + std::to_string(step), std::to_string(different) + " of " + std::to_string(partWidth * partHeight) + " texels differ", ok);
			}
		};

		std::cout << "\nchunks: a part of the map, also every 2nd and 4th sample, equals the whole map bit for bit\n";
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

	//Gradient (dx, dy) and Laplacian of h per world unit, as surface_compute.shader writes them
	struct Derivatives {
		float dx, dy, laplacian, reserved;
	};

	//Runs the surface pass on h(x, y) given in texels and checks every texel against the expected derivatives
	bool CheckSurfaceCase(GpuSurfacePass& pass, const std::string& name, const std::function<float(float, float)>& h,
		const std::function<Derivatives(int, int)>& expected)
	{
		//Small and not square; the borders are a large part of it, so one-sided differences are covered
		constexpr int width = 7, height = 5;
		HeightField input(width, height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				input.At(x, y) = h(static_cast<float>(x), static_cast<float>(y));
			}
		}
		TextureClass inputTexture(width, height, GL_R32F), output(width, height, GL_RGBA32F);
		inputTexture.Upload(input.Data(), GL_RED, GL_FLOAT);
		pass.Run(inputTexture, surfaceTexelSize, output);
		std::vector<Derivatives> result(static_cast<size_t>(width) * height);
		output.Read(GL_RGBA, GL_FLOAT, static_cast<int>(result.size() * sizeof(Derivatives)), result.data());

		float worst = 0.0f;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const Derivatives& d = result[static_cast<size_t>(y) * width + x];
				const Derivatives e = expected(x, y);
				worst = std::max({ worst, std::abs(d.dx - e.dx), std::abs(d.dy - e.dy), std::abs(d.laplacian - e.laplacian) });
				if (!std::isfinite(d.dx) || !std::isfinite(d.dy) || !std::isfinite(d.laplacian)) {
					worst = INFINITY;
				}
			}
		}
		//Inputs are small integers and halves, so the differences are exact up to a rounding or two
		const bool ok = worst <= 1e-5f;
		std::ostringstream details;
		details << "largest error " << std::scientific << std::setprecision(1) << worst;
		Report(name, details.str(), ok);
		return ok;
	}

	bool CheckSurface(GpuSurfacePass& pass)
	{
		std::cout << "\nsurface map: derivatives of maps with known derivatives\n";
		constexpr float t = surfaceTexelSize;
		bool ok = CheckSurfaceCase(pass, "surface: flat", [](float, float) { return 0.3f; },
			[](int, int) { return Derivatives{ 0.0f, 0.0f, 0.0f, 0.0f }; });
		//Rises by 0.5 per texel along x and falls by 0.25 along y; the same gradient on the borders
		ok = CheckSurfaceCase(pass, "surface: plane", [](float x, float y) { return 0.5f * x - 0.25f * y; },
			[](int, int) { return Derivatives{ 0.5f / t, -0.25f / t, 0.0f, 0.0f }; }) && ok;
		//h = x^2 + y^2: second difference 2 along each axis; inside, the central difference of x^2 is exactly 2x
		ok = CheckSurfaceCase(pass, "surface: bowl", [](float x, float y) { return x * x + y * y; },
			[](int x, int y) {
				const auto first = [](int v, int size) {
					//One-sided at the borders: (v + 1)^2 - v^2 or v^2 - (v - 1)^2
					return v == 0 ? 1.0f : v == size - 1 ? static_cast<float>(2 * v - 1) : static_cast<float>(2 * v);
				};
				return Derivatives{ first(x, 7) / t, first(y, 5) / t, 4.0f / (t * t), 0.0f };
			}) && ok;
		return ok;
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
		const BiomeConfig biomes = BiomeConfig::Default();
		const std::vector<biome::Biome> table = biome::DefaultBiomes();
		std::cout << "\nGPU time [ms]   terrain   biomes   surface map\n";
		for (int size : { 512, 1024, 2048, 4096 }) {
			TextureClass height(size, size, GL_R32F), colors(size, size, GL_RGBA32F), ids(size, size, GL_R32I), surface(size, size, GL_RGBA32F);
			const double terrainMs = MedianMs([&] {
				gpu.GenerateTerrain(config, MapArea{}, height);
				glFinish();
			}, 9);
			const double biomesMs = MedianMs([&] {
				gpu.GenerateBiomes(biomes, config, table, MapArea{}, colors, ids);
				glFinish();
			}, 9);
			const double surfaceMs = MedianMs([&] {
				surfacePass.Run(height, surfaceTexelSize, surface);
				glFinish();
			}, 9);
			std::cout << std::left << std::setw(14) << (std::to_string(size) + "x" + std::to_string(size)) << std::right << std::fixed << std::setprecision(2)
				<< std::setw(10) << terrainMs << std::setw(9) << biomesMs << std::setw(14) << surfaceMs << std::defaultfloat << "\n" << std::left;
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
			bool ok = CheckRanges(gpu);
			ok = CheckDeterminism(gpu) && ok;
			ok = CheckBiomes(gpu) && ok;
			ok = CheckChunks(gpu) && ok;
			ok = CheckSurface(surfacePass) && ok;
			RunTimings(gpu, surfacePass);
			std::cout << "\n" << (ok ? "All checks passed" : "[ERROR] Some checks failed") << std::endl;
			result = ok ? 0 : 1;
		}
	}
	glfwDestroyWindow(window);
	glfwTerminate();
	return result;
}
