#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "glm/glm.hpp"

#include "Frustum.h"
#include "GpuHeightGenerator.h"
#include "GpuSurfacePass.h"
#include "Grid.h"
#include "TerrainView.h"

//Endless terrain around the camera ("Infinite generation") as a quadtree of chunks with levels of detail.
//A chunk of level L covers chunkSize * 2^L map texels with the same textureSize^2 textures, so it samples every 2^L-th
//position; samples lie on integer positions, so where levels meet they are equal. A chunk closer to the camera than
//lodDistance times its size is drawn as its four children, but only once all four exist, so the view has no holes.
//Missing and outdated chunks are generated a few per frame, those in the view frustum first, then nearest first; only chunks
//in the frustum are drawn. Chunks out of range stay cached up to a limit.
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
	//@param colors, ids, splat - GL_RGBA32F, GL_R32I and the splat map (GpuHeightGenerator::GenerateBiomes), nullptr while biomes are off
	using GenerateFn = std::function<void(const MapArea& area, TextureClass& height, TextureClass* colors, TextureClass* ids, TextureClass* splat)>;

	bool Initialize();
	//Rebuilds the shaders from their source files and regenerates the chunks
	bool ReloadShader();
	//Level of detail settings
	void ImGuiDraw();

	//Every chunk has to be generated again; old contents stay shown until replaced.
	//Starts measuring how long the view takes to show the change
	void Invalidate();
	//Biome maps are generated only while enabled; a change invalidates the chunks
	void SetBiomes(bool enabled);
	//Puts map texel (0, 0) where TerrainView draws it for a map of this size, and makes islands fill that size;
	//a change invalidates the chunks
	void AlignWithMap(int width, int height);

	//Where the world is seen from; all in world units before modelScale
	struct View {
		glm::vec2 center = glm::vec2(0.0f);	//camera XZ
		float radius = 0.0f;				//view distance
		float heightScale = 1.0f;			//bounds the height of the chunks for the frustum test
		std::optional<Frustum> frustum;		//none = everything in range is in view (2D map)
	};
	//View of the camera as drawn with these settings
	static View ViewFrom(const Camera& camera, const TerrainRenderSettings& settings);

	//Picks the chunks to draw and frees cached chunks over the limit. Call every frame before GenerateNext and Draw
	//@return number of chunks waiting for generation
	int Plan(const View& view);
	//Generates up to maxChunksPerFrame of the chunks picked by Plan, nearest first, with their surface maps.
	//They are drawn from the next Plan on, when a whole set of children can replace its parent
	void GenerateNext(const GenerateFn& generate);

	void Draw(Renderer& renderer, Camera& camera, LightSource& light, const TerrainRenderSettings& settings);

	//Id of the biome at a world position (XZ before modelScale), -1 if not generated yet.
	//Reads the finest chunk there, which is the one under the camera; copies its ids from the GPU when it changes
	int BiomeAt(glm::vec2 position);

	//A generated chunk of level 0 (full detail) with its maps
	struct FineChunk {
		uint64_t key = 0;
		glm::ivec2 coord = glm::ivec2(0);		//in chunks; its first map texel is coord * chunkSize
		uint64_t generation = 0;				//changes whenever the chunk is generated again
		const TextureClass* height = nullptr;
		const TextureClass* surface = nullptr;
		const TextureClass* ids = nullptr;		//nullptr while biomes are off
		float distance = 0.0f;					//from the camera of the last Plan, XZ in world units
	};
	//Generated chunks of level 0 within maxDistance of the camera of the last Plan, nearest first; drawn or not
	std::vector<FineChunk> FineChunks(float maxDistance) const;
	//World XZ (before modelScale) of map texel (0, 0)
	glm::vec2 GetMapOrigin() const { return mapOrigin; }

	//Chunks drawn: in range and in the frustum
	int GetVisibleCount() const { return static_cast<int>(visible.size()); }
	//Chunks that would be drawn without the frustum test
	int GetInRangeCount() const { return inRange; }
	int GetCachedCount() const { return static_cast<int>(chunks.size()); }
	int GetPendingCount() const { return static_cast<int>(pending.size()); }
	//Coarsest level among the chunks drawn
	int GetCoarsestLevel() const { return coarsestLevel; }
	//Part of the view has nothing to draw: a chunk in view has neither its own maps nor a generated replacement
	bool HasHoleInView() const { return holeInView; }
	//Output panel: time from the last change (Invalidate) until every chunk drawn showed it, and until no chunk in view waited (full detail)
	void ImGuiUpdateTimes() const;

private:
	struct Chunk {
		int level = 0;
		glm::ivec2 coord = glm::ivec2(0);	//in chunks of its level
		std::unique_ptr<TextureClass> height, surface, colors, ids, splat;
		uint64_t version = 0;		//worldVersion it was generated for; 0 = never generated
		uint64_t generation = 0;	//unique number of its last generation
		uint64_t lastSeen = 0;		//frame of the last Plan that visited it
	};
	struct PendingChunk {
		int level;
		glm::ivec2 coord;
		float distance;
		//Order of generation: 0 = leaves a hole in view, 1 = a hole out of view, 2 = in view, 3 = out of view
		int rank;
	};

	static uint64_t Key(int level, glm::ivec2 coord);
	//Size of a chunk of a level in world units
	static float NodeSize(int level);
	Chunk* Find(int level, glm::ivec2 coord);
	//Visits a chunk within radius: draws it, or its children if they should and can replace it, and requests what is missing
	//@param local - camera relative to map texel (0, 0)
	void Select(int level, glm::ivec2 coord, glm::vec2 local, const View& view);
	//Whether the chunk can be seen; the box spans all heights a map can have
	bool InView(int level, glm::ivec2 coord, const View& view) const;
	//Existing chunk, or a new one with textures of a freed chunk if there is one
	Chunk& Obtain(int level, glm::ivec2 coord);
	void AllocateTextures(Chunk& chunk);
	//Called by Plan: finishes the measurement of the last change when the view shows it
	void MeasureChange();
	void BuildMesh();

	std::unordered_map<uint64_t, Chunk> chunks;
	//Textures of freed chunks, reused for new ones
	std::vector<Chunk> spare;
	//Chunks to draw (pointers stay valid: removing chunks happens only in Plan, before this is filled)
	std::vector<Chunk*> visible;
	std::vector<PendingChunk> pending;
	size_t visited = 0;
	int inRange = 0;
	bool holeInView = false;

	//Measuring a change: time of the last Invalidate, results (negative = not yet)
	std::chrono::steady_clock::time_point changeTime;
	bool measuringChange = false;
	double updateMs = -1.0, fullDetailMs = -1.0;	//from the last change until the view shows it / until full detail
	int measuredChanges = 0;
	int coarsestLevel = 0;

	//A chunk closer to the camera than this many of its sizes is replaced by its children
	float lodDistance = 3.0f;

	uint64_t worldVersion = 1, generationCounter = 0, frame = 0;
	bool biomes = false;
	glm::ivec2 alignedSize = glm::ivec2(0);
	glm::vec2 mapOrigin = glm::vec2(0.0f);
	glm::vec2 lastCenter = glm::vec2(0.0f);
	float lastRadius = 0.0f;
	//Distance up to which the last Plan visited chunks, a little past the view distance
	float planRadius = 0.0f;

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
