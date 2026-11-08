#include "ChunkWorld.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "glm/gtc/matrix_transform.hpp"

namespace {
	//Texels along a side of one patch of the chunk mesh; the TCS divides a patch into up to 64 segments
	constexpr int texelsPerPatch = 16;
	constexpr int patchesPerSide = ChunkWorld::chunkSize / texelsPerPatch;
	static_assert(ChunkWorld::chunkSize % texelsPerPatch == 0);
	//Plan looks at most this many chunks away from the camera, whatever the view distance
	constexpr int maxChunkRadius = 32;
	//Freed chunks whose textures are kept for reuse
	constexpr size_t maxSpare = 32;
}

bool ChunkWorld::Initialize()
{
	shader = TerrainView::CreateTerrainShader();
	if (!shader->IsValid() || !surfacePass.Initialize()) {
		std::cout << "[ERROR] ChunkWorld: a shader could not be built" << std::endl;
		return false;
	}
	const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	noBiomes = std::make_unique<TextureClass>(1, 1, GL_RGBA32F);
	noBiomes->Upload(black, GL_RGBA, GL_FLOAT);

	vao = std::make_unique<VertexArray>();
	layout.Push<float>(3);
	layout.Push<float>(2);
	BuildMesh();
	return true;
}

bool ChunkWorld::ReloadShader()
{
	surfacePass.ReloadShader();
	Invalidate();
	return shader->Reload();
}

//Patches in the order of utilities::GenerateVerticesForResolution: (x, z), (x + 1, z), (x, z + 1), (x + 1, z + 1).
//Vertices sit on texels: chunk texel t is texel t + 1 of the textures, sampled at its center
void ChunkWorld::BuildMesh()
{
	const float step = texelsPerPatch * TerrainView::worldUnitsPerTexel;
	const auto texCoord = [](int patch) { return (static_cast<float>(patch * texelsPerPatch) + 1.5f) / static_cast<float>(textureSize); };
	std::vector<float> vertices;
	for (int i = 0; i < patchesPerSide; i++) {
		for (int j = 0; j < patchesPerSide; j++) {
			for (auto [di, dj] : { std::pair{ 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }) {
				vertices.insert(vertices.end(), { (i + di) * step, 0.0f, (j + dj) * step, texCoord(i + di), texCoord(j + dj) });
			}
		}
	}
	patchCount = patchesPerSide * patchesPerSide;
	vertexBuffer = std::make_unique<VertexBuffer>(vertices.data(), static_cast<unsigned int>(vertices.size() * sizeof(float)));
	vao->AddBuffer(*vertexBuffer, layout);
}

void ChunkWorld::SetBiomes(bool enabled)
{
	if (enabled != biomes) {
		biomes = enabled;
		Invalidate();
	}
}

void ChunkWorld::AlignWithMap(int width, int height)
{
	if (glm::ivec2(width, height) == alignedSize) {
		return;
	}
	alignedSize = glm::ivec2(width, height);
	//TerrainView centers the map at the world origin and samples texel i at its center
	mapOrigin = (-glm::vec2(alignedSize) / 2.0f + 0.5f) * TerrainView::worldUnitsPerTexel;
	Invalidate();
}

uint64_t ChunkWorld::Key(glm::ivec2 coord)
{
	return (static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) << 32) | static_cast<uint32_t>(coord.y);
}

int ChunkWorld::Plan(glm::vec2 center, float radius)
{
	frame++;
	visible.clear();
	pending.clear();

	const float size = ChunkWorldSize();
	radius = std::clamp(radius, 0.0f, maxChunkRadius * size);
	lastCenter = center;
	lastRadius = radius;

	//Camera relative to map texel (0, 0)
	const glm::vec2 local = center - mapOrigin;
	const glm::ivec2 low(glm::floor((local - radius) / size));
	const glm::ivec2 high(glm::floor((local + radius) / size));
	size_t inRange = 0;
	for (int z = low.y; z <= high.y; z++) {
		for (int x = low.x; x <= high.x; x++) {
			const glm::vec2 cornerMin = glm::vec2(x, z) * size;
			const float distance = glm::length(local - glm::clamp(local, cornerMin, cornerMin + size));
			if (distance > radius) {
				continue;
			}
			inRange++;
			const auto found = chunks.find(Key({ x, z }));
			if (found == chunks.end()) {
				pending.push_back({ { x, z }, distance });
				continue;
			}
			Chunk& chunk = found->second;
			chunk.lastSeen = frame;
			if (chunk.version != 0) {
				visible.push_back(&chunk);
			}
			if (chunk.version != worldVersion) {
				pending.push_back({ { x, z }, distance });
			}
		}
	}
	std::sort(pending.begin(), pending.end(), [](const PendingChunk& a, const PendingChunk& b) { return a.distance < b.distance; });

	//Keeps half as many chunks out of range as in it, longest unseen go first
	const size_t limit = inRange + inRange / 2 + 16;
	if (chunks.size() > limit) {
		std::vector<std::pair<uint64_t, uint64_t>> unseen;
		for (const auto& [key, chunk] : chunks) {
			if (chunk.lastSeen != frame) {
				unseen.push_back({ chunk.lastSeen, key });
			}
		}
		std::sort(unseen.begin(), unseen.end());
		for (size_t i = 0; i < unseen.size() && chunks.size() > limit; i++) {
			const auto found = chunks.find(unseen[i].second);
			if (spare.size() < maxSpare) {
				spare.push_back(std::move(found->second));
			}
			chunks.erase(found);
		}
	}
	return static_cast<int>(pending.size());
}

ChunkWorld::Chunk& ChunkWorld::Obtain(glm::ivec2 coord)
{
	const uint64_t key = Key(coord);
	auto found = chunks.find(key);
	if (found != chunks.end()) {
		return found->second;
	}
	Chunk chunk;
	if (!spare.empty()) {
		chunk = std::move(spare.back());
		spare.pop_back();
	}
	chunk.coord = coord;
	chunk.version = 0;
	chunk.lastSeen = frame;
	return chunks.emplace(key, std::move(chunk)).first->second;
}

void ChunkWorld::AllocateTextures(Chunk& chunk)
{
	if (!chunk.height) {
		chunk.height = std::make_unique<TextureClass>(textureSize, textureSize, GL_R32F);
		chunk.surface = std::make_unique<TextureClass>(textureSize, textureSize, GL_RGBA32F);
	}
	if (biomes && !chunk.colors) {
		chunk.colors = std::make_unique<TextureClass>(textureSize, textureSize, GL_RGBA32F);
		chunk.ids = std::make_unique<TextureClass>(textureSize, textureSize, GL_R32I);
	}
	else if (!biomes) {
		chunk.colors.reset();
		chunk.ids.reset();
	}
}

void ChunkWorld::GenerateNext(const GenerateFn& generate)
{
	const int count = std::min(static_cast<int>(pending.size()), maxChunksPerFrame);
	for (int i = 0; i < count; i++) {
		Chunk& chunk = Obtain(pending[i].coord);
		AllocateTextures(chunk);
		const glm::ivec2 origin = chunk.coord * chunkSize - 1;
		generate(static_cast<float>(origin.x), static_cast<float>(origin.y), *chunk.height, chunk.colors.get(), chunk.ids.get());
		surfacePass.Run(*chunk.height, TerrainView::worldUnitsPerTexel, *chunk.surface);
		if (chunk.version == 0) {
			visible.push_back(&chunk);
		}
		chunk.version = worldVersion;
		chunk.generation = ++generationCounter;
	}
	pending.erase(pending.begin(), pending.begin() + count);
}

void ChunkWorld::Draw(Renderer& renderer, Camera& camera, LightSource& light, const TerrainRenderSettings& settings)
{
	if (visible.empty()) {
		return;
	}
	const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(settings.modelScale));
	//The 2D map frames the area in view around the camera
	TerrainView::SetFrameUniforms(*shader, camera, light, settings, scale, 0, lastCenter - lastRadius, lastCenter + lastRadius);

	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	const float size = ChunkWorldSize();
	for (const Chunk* chunk : visible) {
		const glm::vec2 corner = mapOrigin + glm::vec2(chunk->coord) * size;
		shader->SetModel(glm::translate(scale, glm::vec3(corner.x, 0.0f, corner.y)));
		chunk->height->Bind(0);
		(chunk->colors ? *chunk->colors : *noBiomes).Bind(static_cast<unsigned int>(TerrainView::Layer::BIOMES));
		chunk->surface->Bind(TerrainView::surfaceUnit);
		renderer.DrawPatches(*vao, *shader, patchCount, 4);
	}
	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
}

int ChunkWorld::BiomeAt(glm::vec2 position)
{
	if (!biomes) {
		return -1;
	}
	const glm::ivec2 texel(glm::floor((position - mapOrigin) / TerrainView::worldUnitsPerTexel + 0.5f));
	const glm::ivec2 coord(glm::floor(glm::vec2(texel) / static_cast<float>(chunkSize)));
	const uint64_t key = Key(coord);
	const auto found = chunks.find(key);
	if (found == chunks.end() || found->second.version == 0 || !found->second.ids) {
		return -1;
	}
	const Chunk& chunk = found->second;
	if (biomeCopy.Empty() || biomeCopyKey != key || biomeCopyGeneration != chunk.generation) {
		biomeCopy.Resize(textureSize, textureSize);
		chunk.ids->Read(GL_RED_INTEGER, GL_INT, static_cast<int>(biomeCopy.Size() * sizeof(int)), biomeCopy.Data());
		biomeCopyKey = key;
		biomeCopyGeneration = chunk.generation;
	}
	const glm::ivec2 inChunk = texel - coord * chunkSize + 1;
	return biomeCopy.At(inChunk.x, inChunk.y);
}
