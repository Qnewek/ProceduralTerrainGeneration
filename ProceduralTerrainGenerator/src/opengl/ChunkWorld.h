#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "glm/glm.hpp"

#include "GpuSurfacePass.h"
#include "Grid.h"
#include "TerrainView.h"

//Endless terrain made of square chunks around the camera ("Infinite generation"). Missing and outdated chunks are generated
//nearest first, a few per frame; chunks out of view stay cached until the cache limit, so coming back costs nothing.
//Map position p (in texels, as the origin of GpuHeightGenerator) lies at world XZ mapOrigin + p * worldUnitsPerTexel.
//mapOrigin puts the world over a finite map of the size given to AlignWithMap, so switching modes keeps the terrain in place.
//Only for the thread owning the GL context.
class ChunkWorld
{
public:
	//Texels along a side of a chunk. The textures have one more texel on every side (apron), so normals are
	//continuous across chunk borders; neighbouring chunks share the texels on their common edge
	static constexpr int chunkSize = 128;
	static constexpr int textureSize = chunkSize + 3;
	//Chunks generated per frame at most; a chunk with biomes takes about 0.1 ms of GPU time
	static constexpr int maxChunksPerFrame = 16;

	//Fills the maps of one chunk, textureSize x textureSize texels
	//@param originx, originy - map position of texel (0, 0) of the textures
	//@param height - GL_R32F
	//@param colors, ids - GL_RGBA32F and GL_R32I, nullptr while biomes are off
	using GenerateFn = std::function<void(float originx, float originy, TextureClass& height, TextureClass* colors, TextureClass* ids)>;

	bool Initialize();
	//Rebuilds the shaders from their source files and regenerates the chunks
	bool ReloadShader();

	//Every chunk has to be generated again; old contents stay shown until replaced
	void Invalidate() { worldVersion++; }
	//Biome maps are generated only while enabled; a change invalidates the chunks
	void SetBiomes(bool enabled);
	//Puts map texel (0, 0) where TerrainView draws it for a map of this size; a change invalidates the chunks
	void AlignWithMap(int width, int height);

	//Picks the chunks within radius of center and frees cached chunks over the limit. Call every frame before GenerateNext and Draw
	//@param center - camera XZ in world units (before modelScale)
	//@param radius - view distance in world units (before modelScale)
	//@return number of chunks waiting for generation
	int Plan(glm::vec2 center, float radius);
	//Generates up to maxChunksPerFrame of the chunks picked by Plan, nearest first, with their surface maps
	void GenerateNext(const GenerateFn& generate);

	void Draw(Renderer& renderer, Camera& camera, LightSource& light, const TerrainRenderSettings& settings);

	//Id of the biome at a world position (XZ before modelScale), -1 if not generated yet.
	//Copies the ids of the chunk from the GPU when the camera enters it or it is regenerated
	int BiomeAt(glm::vec2 position);

	int GetVisibleCount() const { return static_cast<int>(visible.size()); }
	int GetCachedCount() const { return static_cast<int>(chunks.size()); }
	int GetPendingCount() const { return static_cast<int>(pending.size()); }

private:
	struct Chunk {
		glm::ivec2 coord = glm::ivec2(0);
		std::unique_ptr<TextureClass> height, surface, colors, ids;
		uint64_t version = 0;		//worldVersion it was generated for; 0 = never generated
		uint64_t generation = 0;	//unique number of its last generation
		uint64_t lastSeen = 0;		//frame of the last Plan that had it in range
	};
	struct PendingChunk {
		glm::ivec2 coord;
		float distance;
	};

	static uint64_t Key(glm::ivec2 coord);
	float ChunkWorldSize() const { return chunkSize * TerrainView::worldUnitsPerTexel; }
	//Existing chunk at coord, or a new one with textures of a freed chunk if there is one
	Chunk& Obtain(glm::ivec2 coord);
	void AllocateTextures(Chunk& chunk);
	void BuildMesh();

	std::unordered_map<uint64_t, Chunk> chunks;
	//Textures of freed chunks, reused for new ones
	std::vector<Chunk> spare;
	//Chunks with contents within view (pointers stay valid: removing chunks happens only in Plan, before this is filled)
	std::vector<Chunk*> visible;
	std::vector<PendingChunk> pending;

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
	//Patch grid of one chunk in its own coordinates; Draw moves it with the model matrix
	VertexBufferLayout layout;
	std::unique_ptr<VertexArray> vao;
	std::unique_ptr<VertexBuffer> vertexBuffer;
	int patchCount = 0;
};
