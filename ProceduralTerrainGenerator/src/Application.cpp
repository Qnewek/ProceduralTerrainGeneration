#include "TerrainGenApp.h"
#include "TerrainGenerator.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

//FNV-1a hash of a float map, used to verify that generation output is unchanged between versions
static uint64_t MapChecksum(const float* map, int width, int height)
{
	uint64_t hash = 14695981039346656037ull;
	const unsigned char* bytes = reinterpret_cast<const unsigned char*>(map);
	for (size_t i = 0; i < static_cast<size_t>(width) * height * sizeof(float); i++) {
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

//Runs fn repeatedly (at least once, until ~1 s elapsed or 5 runs) and returns the median time in ms
template<typename F>
static double MedianMs(F fn)
{
	std::vector<double> times;
	double total = 0.0;
	while (times.empty() || (total < 1000.0 && times.size() < 5)) {
		auto start = std::chrono::steady_clock::now();
		fn();
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		times.push_back(ms);
		total += ms;
	}
	std::sort(times.begin(), times.end());
	return times[times.size() / 2];
}

//Headless benchmark of CPU terrain generation, run with: ProceduralTerrainGenerator.exe --benchmark
static int RunBenchmark()
{
	std::cout << "size        noises [ms]   terrain [ms]   terrain checksum\n";
	for (int size : { 256, 512, 1024 }) {
		//Generators log every call - silence them while measuring
		std::ostringstream sink;
		std::streambuf* original = std::cout.rdbuf(sink.rdbuf());

		const TerrainConfig config = TerrainConfig::Default();
		TerrainGenerator gen;
		bool ok = gen.Initialize(size, size);
		double noisesMs = MedianMs([&] { gen.GenerateNoises(config); });
		double terrainMs = MedianMs([&] { gen.GenerateTerrain(config, 0.0f, 0.0f); });

		std::cout.rdbuf(original);
		if (!ok) {
			std::cout << "[ERROR] TerrainGenerator initialization failed for size " << size << std::endl;
			return -1;
		}
		std::cout << std::left << std::setw(12) << (std::to_string(size) + "x" + std::to_string(size))
			<< std::right << std::fixed << std::setprecision(1)
			<< std::setw(11) << noisesMs << std::setw(15) << terrainMs
			<< "   " << std::hex << MapChecksum(gen.GetHeightMap(), size, size) << std::dec << std::endl;
	}
	return 0;
}

int main(int argc, char* argv[])
{
	if (argc > 1 && std::strcmp(argv[1], "--benchmark") == 0) {
		return RunBenchmark();
	}

	TerrainGenApp* app = new TerrainGenApp();
	if (!app->Initialize()) {
		std::cout << "[ERROR] Application initialization failed" << std::endl;
		delete app;
		return -1;
	}
	app->Start();
	delete app;
    return 0;
}
