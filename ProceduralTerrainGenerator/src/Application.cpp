#include "TerrainGenApp.h"
#include "TerrainGenerator.h"
#include "BiomeGenerator.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

//FNV-1a hash of a map, used to verify that generation output is unchanged between versions
//@param hash - result of a previous call to chain several maps into one checksum
template<typename T>
static uint64_t MapChecksum(const Grid<T>& map, uint64_t hash = 14695981039346656037ull)
{
	const unsigned char* bytes = reinterpret_cast<const unsigned char*>(map.Data());
	for (size_t i = 0; i < map.Size() * sizeof(T); i++) {
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
	std::cout << "hardware threads: " << std::thread::hardware_concurrency() << "\n";
	std::cout << "size        noises [ms]   terrain [ms]   biomes [ms]   terrain checksum   noises checksum    biomes checksum\n";
	for (int size : { 256, 512, 1024 }) {
		//Generators log every call - silence them while measuring
		std::ostringstream sink;
		std::streambuf* original = std::cout.rdbuf(sink.rdbuf());

		const TerrainConfig config = TerrainConfig::Default();
		const BiomeConfig biomeConfig = BiomeConfig::Default();
		TerrainGenerator gen;
		BiomeGenerator biomeGen;
		bool ok = gen.Initialize(size, size) && biomeGen.Initialize(size, size);
		double noisesMs = MedianMs([&] { gen.GenerateNoises(config); });
		double terrainMs = MedianMs([&] { gen.GenerateTerrain(config, 0.0f, 0.0f); });
		//Temperature/humidity noises + biome assignment
		double biomesMs = MedianMs([&] {
			biomeGen.GenerateComponentNoises(biomeConfig);
			biomeGen.Biomify(biomeConfig, gen.GetSelectedNoise(WorldGenParameter::CONTINENTALNESS),
				gen.GetSelectedNoise(WorldGenParameter::MOUNTAINOUSNESS), gen.GetSelectedNoise(WorldGenParameter::WEIRDNESS));
		});

		std::cout.rdbuf(original);
		if (!ok) {
			std::cout << "[ERROR] Generator initialization failed for size " << size << std::endl;
			return -1;
		}
		uint64_t noisesChecksum = MapChecksum(gen.GetSelectedNoise(WorldGenParameter::CONTINENTALNESS).GetField());
		noisesChecksum = MapChecksum(gen.GetSelectedNoise(WorldGenParameter::MOUNTAINOUSNESS).GetField(), noisesChecksum);
		noisesChecksum = MapChecksum(gen.GetSelectedNoise(WorldGenParameter::WEIRDNESS).GetField(), noisesChecksum);

		std::cout << std::left << std::setw(12) << (std::to_string(size) + "x" + std::to_string(size))
			<< std::right << std::fixed << std::setprecision(1)
			<< std::setw(11) << noisesMs << std::setw(15) << terrainMs << std::setw(14) << biomesMs
			<< std::hex << std::setfill('0')
			<< "   " << std::setw(16) << MapChecksum(gen.GetHeightField())
			<< "   " << std::setw(16) << noisesChecksum
			<< "   " << std::setw(16) << MapChecksum(biomeGen.GetBiomeMap())
			<< std::dec << std::setfill(' ') << std::endl;
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
