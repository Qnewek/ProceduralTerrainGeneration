#pragma once

#include "TerrainObjects/Chunk.h"
#include "MultiNoiseSimulation.h"
#include "Noise.h"

class TerrainGenerator
{	
private:
	std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> activeChunks;
	int renderDistance = 8;
	float SCALE = 1.0f;
	ChunkCoord lastPlayerChunk = { -9999, -9999 };

	noise::SimplexNoiseClass noise;
	MultiNoiseSimulation multiNoiseSim;
public:
    TerrainGenerator() {
        noise.Initialize();
        multiNoiseSim.Initialize();
    }
	~TerrainGenerator() = default;

    void Update(float playerX, float playerZ) {
        int pCx = static_cast<int>(std::floor(playerX / (Chunk::SIZE * SCALE)));
        int pCz = static_cast<int>(std::floor(playerZ / (Chunk::SIZE * SCALE)));
        ChunkCoord currentChunk = { pCx, pCz };

        if (currentChunk == lastPlayerChunk) return;

        lastPlayerChunk = currentChunk;

        for (int x = -renderDistance; x <= renderDistance; ++x) {
            for (int z = -renderDistance; z <= renderDistance; ++z) {
                ChunkCoord target = { pCx + x, pCz + z };

                if (activeChunks.find(target) == activeChunks.end()) {
                    auto newChunk = std::make_unique<Chunk>(target);
                    newChunk->GenerateMesh(noise);
                    activeChunks[target] = std::move(newChunk);
                }
            }
        }

        int unloadDistance = renderDistance + 2;
        int unloadDistSq = unloadDistance * unloadDistance;

        auto it = activeChunks.begin();
        while (it != activeChunks.end()) {
            ChunkCoord c = it->first;
            int dx = c.x - pCx;
            int dz = c.z - pCz;
            int distSq = dx * dx + dz * dz;

            if (distSq > unloadDistSq) {
                it = activeChunks.erase(it);
            }
            else {
                ++it;
            }
        }
    }
    const auto& GetChunks() const { return activeChunks; }
};