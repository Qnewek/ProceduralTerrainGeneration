#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "glm/glm.hpp"

#include "GpuHeightGenerator.h"
#include "GpuSurfacePass.h"
#include "Grid.h"
#include "TerrainView.h"

//Endless terrain around the camera ("Infinite generation") as a quadtree of chunks with levels of detail.
//A chunk of level L covers chunkSize * 2^L map texels with the same textureSize^2 textures, so it samples every 2^L-th
//position; samples lie on integer positions, so where levels meet they are equal. A chunk closer to the camera than
//lodDistance times its size is drawn as its four children, but only once all four exist, so the view has no holes.
//Missing and outdated chunks are generated nearest first, a few per frame; chunks out of view stay cached up to a limit.
//Map position p (in texels) lies at world XZ mapOrigin + p * worldUnitsPerTexel. mapOrigin puts the world over a finite map
//of the size given to AlignWithMap, so switching modes keeps the terrain in place. Only for the thread owning the GL context.
class ChunkWorld
{
public:
	//Texels along a side of a chunk. The textures have one more texel on every side (apron), so normals are
	//continuous across borders of chunks of the same level; neighbouring chunks share the texels on their common edge
	static constexpr int chunkSize = 128;
	static constexpr int textureSize = chunkSize + 3;
	//Chunks generated per frame at most; a chunk with biomes takes about 0.1 ms of GPU time
	static constexpr int maxChunksPerFrame = 16;
	//Coarsest level: a chunk of it spans chunkSize * 2^maxLevel texels
	static constexpr int maxLevel = 10;

	//Fills the maps of one chunk, textureSize x textureSize texels
	//@param area - map positions of the texels and the island size, for GpuHeightGenerator
	//@param height - GL_R32F
	//@param colors, ids - GL_RGBA32F and GL_R32I, nullptr while biomes are off
	using GenerateFn = std::function<void(const MapArea& area, TextureClass& height, TextureClass* colors, TextureClass* ids)>;

	bool Initialize();
	//Rebuilds the shaders from their source files and regenerates the chunks
	bool ReloadShader();
	//Level of detail settings
	void ImGuiDraw();

	//Every chunk has to be generated again; old contents stay shown until replaced
	void Invalidate() { worldVersion++; }
	//Biome maps are generated only while enabled; a change invalidates the chunks
	void SetBiomes(bool enabled);
	//Puts map texel (0, 0) where TerrainView draws it for a map of this size, and makes islands fill that size;
	//a change invalidates the chunks
	void AlignWithMap(int width, int height);

	//Picks the chunks to draw within radius of center and frees cached chunks over the limit. Call every frame before GenerateNext and Draw
	//@param center - camera XZ in world units (before modelScale)
	//@param radius - view distance in world units (before modelScale)
	//@return number of chunks waiting for generation
	int Plan(glm::vec2 center, float radius);
	//Generates up to maxChunksPerFrame of the chunks picked by Plan, nearest first, with their surface maps.
	//They are drawn from the next Plan on, when a whole set of children can replace its parent
	void GenerateNext(const GenerateFn& generate);

	void Draw(Renderer& renderer, Camera& camera, LightSource& light, const TerrainRenderSettings& settings);

	//Id of the biome at a world position (XZ before modelScale), -1 if not generated yet.
	//Reads the finest chunk there, which is the one under the camera; copies its ids from the GPU when it changes
	int BiomeAt(glm::vec2 position);

	int GetVisibleCount() const { return static_cast<int>(visible.size()); }
	int GetCachedCount() const { return static_cast<int>(chunks.size()); }
	int GetPendingCount() const { return static_cast<int>(pending.size()); }
	//Coarsest level among the chunks drawn
	int GetCoarsestLevel() const { return coarsestLevel; }

private:
	struct Chunk {
		int level = 0;
		glm::ivec2 coord = glm::ivec2(0);	//in chunks of its level
		std::unique_ptr<TextureClass> height, surface, colors, ids;
		uint64_t version = 0;		//worldVersion it was generated for; 0 = never generated
		uint64_t generation = 0;	//unique number of its last generation
		uint64_t lastSeen = 0;		//frame of the last Plan that visited it
	};
	struct PendingChunk {
		int level;
		glm::ivec2 coord;
		float distance;
	};

	static uint64_t Key(int level, glm::ivec2 coord);
	//Size of a chunk of a level in world units
	static float NodeSize(int level);
	Chunk* Find(int level, glm::ivec2 coord);
	//Visits a chunk within radius: draws it, or its children if they should and can replace it, and requests what is missing
	//@param local - camera relative to map texel (0, 0)
	void Select(int level, glm::ivec2 coord, glm::vec2 local, float radius);
	//Existing chunk, or a new one with textures of a freed chunk if there is one
	Chunk& Obtain(int level, glm::ivec2 coord);
	void AllocateTextures(Chunk& chunk);
	void BuildMesh();

	std::unordered_map<uint64_t, Chunk> chunks;
	//Textures of freed chunks, reused for new ones
	std::vector<Chunk> spare;
	//Chunks to draw (pointers stay valid: removing chunks happens only in Plan, before this is filled)
	std::vector<Chunk*> visible;
	std::vector<PendingChunk> pending;
	size_t visited = 0;
	int coarsestLevel = 0;

	//A chunk closer to the camera than this many of its sizes is replaced by its children
	float lodDistance = 3.0f;

	uint64_t worldVersion = 1, generationCounter = 0, frame = 0;
	bool biomes = false;
	glm::ivec2 alignedSize = glm::ivec2(0);
	glm::vec2 mapOrigin = glm::vec2(0.0f);
	glm::vec2 lastCenter = glm::vec2(0.0f);
	float lastRadius = 0.0f;

	//Copy of the biome ids of one chunk on the CPU
	BiomeMap biomeCopy;
	uint64_t biomeCopyKey = 0, biomeCopyGeneration = 0;

	GpuSurfacePass surfacePass;
	std::unique_ptr<Shader> shader;
	//Bound as biome colors for chunks without them, e.g. right after biomes were turned on
	std::unique_ptr<TextureClass> noBiomes;
	//Patch grid of a level 0 chunk in its own coordinates, with skirts along the borders; Draw scales and moves it
	VertexBufferLayout layout;
	std::unique_ptr<VertexArray> vao;
	std::unique_ptr<VertexBuffer> vertexBuffer;
	int patchCount = 0;
};
