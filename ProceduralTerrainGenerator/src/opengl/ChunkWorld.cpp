#include "ChunkWorld.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "imgui.h"
#include "glm/gtc/matrix_transform.hpp"

namespace {
	//Texels along a side of one patch of the chunk mesh; the TCS divides a patch into up to 64 segments
	constexpr int texelsPerPatch = 16;
	constexpr int patchesPerSide = ChunkWorld::chunkSize / texelsPerPatch;
	static_assert(ChunkWorld::chunkSize % texelsPerPatch == 0);
	//Freed chunks whose textures are kept for reuse
	constexpr size_t maxSpare = 32;
	//Depth of the skirts, as a part of heightScale for a level 0 chunk; doubles with every level, like the distance between samples
	constexpr float skirtPart = 0.01f;
	constexpr float maxSkirtPart = 0.5f;
	//Chunks are planned this many times farther than the view distance, so they are ready before they come into view
	constexpr float prefetchFactor = 1.15f;
	//Largest absolute height of a map (noises are in [-1, 1]), with a margin; the same bound is used by the TCS
	constexpr float heightBound = 1.5f;
	//Bits of a chunk coordinate in a key
	constexpr int coordBits = 28;
	constexpr uint64_t coordMask = (uint64_t(1) << coordBits) - 1;

	const glm::ivec2 childOffsets[4] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };

	//Distance from a point to a square
	float DistanceToSquare(glm::vec2 point, glm::vec2 cornerMin, float size)
	{
		return glm::length(point - glm::clamp(point, cornerMin, cornerMin + size));
	}
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

void ChunkWorld::ImGuiDraw()
{
	ImGui::SliderFloat("Detail distance", &lodDistance, 1.5f, 6.0f, "%.1f chunks");
	ImGui::SetItemTooltip("A chunk nearer to the camera than this many of its sizes is drawn as four chunks with twice the detail");
}

//Patches in the order of utilities::GenerateVerticesForResolution: (x, z), (x + 1, z), (x, z + 1), (x + 1, z + 1).
//Vertices sit on texels: chunk texel t is texel t + 1 of the textures, sampled at its center. Along the borders hang
//skirts: patches from the border down to y = -1, which the TES moves skirtDepth below the surface. They cover the
//gaps between chunks of different levels, whose borders do not match between the samples of the coarser one
void ChunkWorld::BuildMesh()
{
	const float step = texelsPerPatch * TerrainView::worldUnitsPerTexel;
	const auto texCoord = [](int patch) { return (static_cast<float>(patch * texelsPerPatch) + 1.5f) / static_cast<float>(textureSize); };
	std::vector<float> vertices;
	const auto vertex = [&](int i, float y, int j) {
		vertices.insert(vertices.end(), { i * step, y, j * step, texCoord(i), texCoord(j) });
	};
	for (int i = 0; i < patchesPerSide; i++) {
		for (int j = 0; j < patchesPerSide; j++) {
			for (const glm::ivec2 d : childOffsets) {
				vertex(i + d.x, 0.0f, j + d.y);
			}
		}
	}
	//Skirt patches: the border segment from a to b on top, the same segment at the bottom
	for (int k = 0; k < patchesPerSide; k++) {
		for (const auto& [a, b] : { std::pair{ glm::ivec2(k, 0), glm::ivec2(k + 1, 0) },
				{ glm::ivec2(k, patchesPerSide), glm::ivec2(k + 1, patchesPerSide) },
				{ glm::ivec2(0, k), glm::ivec2(0, k + 1) },
				{ glm::ivec2(patchesPerSide, k), glm::ivec2(patchesPerSide, k + 1) } }) {
			vertex(a.x, 0.0f, a.y);
			vertex(b.x, 0.0f, b.y);
			vertex(a.x, -1.0f, a.y);
			vertex(b.x, -1.0f, b.y);
		}
	}
	patchCount = patchesPerSide * patchesPerSide + 4 * patchesPerSide;
	vertexBuffer = std::make_unique<VertexBuffer>(vertices.data(), static_cast<unsigned int>(vertices.size() * sizeof(float)));
	vao->AddBuffer(*vertexBuffer, layout);
}

void ChunkWorld::Invalidate()
{
	worldVersion++;
	changeTime = std::chrono::steady_clock::now();
	measuringChange = true;
	updateMs = -1.0;
	fullDetailMs = -1.0;
}

void ChunkWorld::MeasureChange()
{
	if (!measuringChange) {
		return;
	}
	const double elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - changeTime).count();
	if (updateMs < 0.0) {
		const bool current = !holeInView && std::all_of(visible.begin(), visible.end(), [this](const Chunk* chunk) { return chunk->version == worldVersion; });
		if (!current) {
			return;
		}
		updateMs = elapsedMs;
	}
	//Ranks 0 and 2 are the chunks in view
	const bool fullDetail = std::none_of(pending.begin(), pending.end(), [](const PendingChunk& p) { return p.rank == 0 || p.rank == 2; });
	if (fullDetail) {
		fullDetailMs = elapsedMs;
		measuringChange = false;
		measuredChanges++;
	}
}

void ChunkWorld::ImGuiUpdateTimes() const
{
	if (measuredChanges == 0 && !measuringChange) {
		return;
	}
	if (updateMs < 0.0) {
		ImGui::Text("Last change: waiting for the view...");
	}
	else if (fullDetailMs < 0.0) {
		ImGui::Text("Last change: in view after %.0f ms, waiting for full detail...", updateMs);
	}
	else {
		ImGui::Text("Last change: in view after %.0f ms, full detail after %.0f ms", updateMs, fullDetailMs);
	}
	ImGui::SetItemTooltip("Measured from the parameter change to the frame whose plan found every drawn chunk current,\n"
		"then until no chunk in view waits for generation. Includes waiting for frames (vsync)");
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

uint64_t ChunkWorld::Key(int level, glm::ivec2 coord)
{
	return (static_cast<uint64_t>(level) << (2 * coordBits))
		| ((static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) & coordMask) << coordBits)
		| (static_cast<uint64_t>(static_cast<uint32_t>(coord.y)) & coordMask);
}

float ChunkWorld::NodeSize(int level)
{
	return static_cast<float>(chunkSize << level) * TerrainView::worldUnitsPerTexel;
}

ChunkWorld::Chunk* ChunkWorld::Find(int level, glm::ivec2 coord)
{
	const auto found = chunks.find(Key(level, coord));
	return found == chunks.end() ? nullptr : &found->second;
}

int ChunkWorld::Plan(const View& requested)
{
	frame++;
	visible.clear();
	pending.clear();
	visited = 0;
	inRange = 0;
	coarsestLevel = 0;

	View view = requested;
	view.radius = std::clamp(view.radius, 0.0f, NodeSize(maxLevel));
	const glm::vec2 center = view.center;
	lastCenter = center;
	lastRadius = view.radius;
	planRadius = view.radius * prefetchFactor;

	//The roots are the smallest chunks larger than the planned distance, so at most 3 x 3 of them are in range
	int rootLevel = 0;
	while (rootLevel < maxLevel && NodeSize(rootLevel) < planRadius) {
		rootLevel++;
	}
	const float rootSize = NodeSize(rootLevel);
	const glm::vec2 local = center - mapOrigin;
	const glm::ivec2 low(glm::floor((local - planRadius) / rootSize));
	const glm::ivec2 high(glm::floor((local + planRadius) / rootSize));
	for (int z = low.y; z <= high.y; z++) {
		for (int x = low.x; x <= high.x; x++) {
			Select(rootLevel, { x, z }, local, view);
		}
	}
	//By rank, then nearest first; a parent is never farther than its children, so coarse chunks come before fine ones
	std::sort(pending.begin(), pending.end(), [](const PendingChunk& a, const PendingChunk& b) {
		if (a.rank != b.rank) {
			return a.rank < b.rank;
		}
		return a.distance < b.distance || (a.distance == b.distance && a.level > b.level);
	});
	holeInView = !pending.empty() && pending.front().rank == 0;
	MeasureChange();

	//Keeps half as many chunks out of range as in it, longest unseen go first
	const size_t limit = visited + visited / 2 + 16;
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

ChunkWorld::View ChunkWorld::ViewFrom(const Camera& camera, const TerrainRenderSettings& settings)
{
	View view;
	const glm::vec3 position = camera.GetPosition() / settings.modelScale;
	view.center = glm::vec2(position.x, position.z);
	view.radius = camera.GetRenderDistance() / settings.modelScale;
	view.heightScale = settings.heightScale;
	//The 2D map shows the whole range from above, not what the camera sees
	if (!settings.map2d) {
		view.frustum.emplace(camera.GetViewProjection() * glm::scale(glm::mat4(1.0f), glm::vec3(settings.modelScale)));
	}
	return view;
}

bool ChunkWorld::InView(int level, glm::ivec2 coord, const View& view) const
{
	if (!view.frustum) {
		return true;
	}
	const float size = NodeSize(level);
	const glm::vec2 corner = mapOrigin + glm::vec2(coord) * size;
	const float scale = std::abs(view.heightScale);
	return view.frustum->Intersects(glm::vec3(corner.x, -(heightBound + maxSkirtPart) * scale, corner.y),
		glm::vec3(corner.x + size, heightBound * scale, corner.y + size));
}

void ChunkWorld::Select(int level, glm::ivec2 coord, glm::vec2 local, const View& view)
{
	const float size = NodeSize(level);
	const float distance = DistanceToSquare(local, glm::vec2(coord) * size, size);
	if (distance > planRadius) {
		return;
	}
	visited++;
	//Chunks between the view distance and planRadius are only prepared; the far plane would cut them off
	const bool inView = distance <= view.radius && InView(level, coord, view);
	Chunk* chunk = Find(level, coord);
	if (chunk) {
		chunk->lastSeen = frame;
	}
	const bool drawable = chunk && chunk->version != 0;

	if (level > 0 && distance < lodDistance * size) {
		//The children replace this chunk only all at once, so nothing is left uncovered while they are generated
		const float childSize = size / 2.0f;
		bool ready = true;
		for (const glm::ivec2 offset : childOffsets) {
			const glm::ivec2 childCoord = coord * 2 + offset;
			const float childDistance = DistanceToSquare(local, glm::vec2(childCoord) * childSize, childSize);
			if (childDistance > planRadius) {
				continue;
			}
			Chunk* child = Find(level - 1, childCoord);
			if (child) {
				child->lastSeen = frame;
				visited++;
			}
			if (!child || child->version == 0) {
				ready = false;
				//Children are requested once there is a parent to show meanwhile
				if (drawable) {
					pending.push_back({ level - 1, childCoord, childDistance, inView && childDistance <= view.radius && InView(level - 1, childCoord, view) ? 2 : 3 });
				}
			}
		}
		if (ready) {
			for (const glm::ivec2 offset : childOffsets) {
				Select(level - 1, coord * 2 + offset, local, view);
			}
			return;
		}
	}

	if (drawable && distance <= view.radius) {
		inRange++;
		if (inView) {
			visible.push_back(chunk);
			coarsestLevel = std::max(coarsestLevel, level);
		}
	}
	if (!chunk || chunk->version != worldVersion) {
		//A chunk with nothing shown in its place goes before all others
		pending.push_back({ level, coord, distance, (drawable ? 2 : 0) + (inView ? 0 : 1) });
	}
}

ChunkWorld::Chunk& ChunkWorld::Obtain(int level, glm::ivec2 coord)
{
	const uint64_t key = Key(level, coord);
	auto found = chunks.find(key);
	if (found != chunks.end()) {
		return found->second;
	}
	Chunk chunk;
	if (!spare.empty()) {
		chunk = std::move(spare.back());
		spare.pop_back();
	}
	chunk.level = level;
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
		chunk.splat = std::make_unique<TextureClass>(textureSize, textureSize, GL_RGBA8, GpuHeightGenerator::splatLayers);
	}
	else if (!biomes) {
		chunk.colors.reset();
		chunk.ids.reset();
		chunk.splat.reset();
	}
}

void ChunkWorld::GenerateNext(const GenerateFn& generate)
{
	const int count = std::min(static_cast<int>(pending.size()), maxChunksPerFrame);
	for (int i = 0; i < count; i++) {
		const PendingChunk& request = pending[i];
		Chunk& chunk = Obtain(request.level, request.coord);
		AllocateTextures(chunk);
		const int step = 1 << chunk.level;
		MapArea area;
		area.origin = glm::vec2(chunk.coord * chunkSize * step - step);
		area.step = static_cast<float>(step);
		area.islandSize = alignedSize;
		generate(area, *chunk.height, chunk.colors.get(), chunk.ids.get(), chunk.splat.get());
		surfacePass.Run(*chunk.height, TerrainView::worldUnitsPerTexel * step, *chunk.surface);
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
	for (const Chunk* chunk : visible) {
		const float step = static_cast<float>(1 << chunk->level);
		const glm::vec2 corner = mapOrigin + glm::vec2(chunk->coord) * NodeSize(chunk->level);
		shader->SetModel(glm::translate(scale, glm::vec3(corner.x, 0.0f, corner.y)));
		shader->SetUniform1f("meshScale", step);
		shader->SetUniform2f("texelWorldSize", TerrainView::worldUnitsPerTexel * step, TerrainView::worldUnitsPerTexel * step);
		shader->SetUniform1f("skirtDepth", std::min(skirtPart * step, maxSkirtPart) * std::abs(settings.heightScale));
		chunk->height->Bind(0);
		(chunk->colors ? *chunk->colors : *noBiomes).Bind(static_cast<unsigned int>(TerrainView::Layer::BIOMES));
		chunk->surface->Bind(TerrainView::surfaceUnit);
		if (chunk->splat) {
			chunk->splat->Bind(TerrainView::splatUnit);
		}
		shader->SetUniform1i("useSplat", chunk->splat ? 1 : 0);
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
	const glm::vec2 texelPosition = (position - mapOrigin) / TerrainView::worldUnitsPerTexel;
	for (int level = 0; level <= maxLevel; level++) {
		const int step = 1 << level;
		//Nearest sample of this level
		const glm::ivec2 sample(glm::floor(texelPosition / static_cast<float>(step) + 0.5f));
		const glm::ivec2 coord(glm::floor(glm::vec2(sample) / static_cast<float>(chunkSize)));
		const uint64_t key = Key(level, coord);
		const auto found = chunks.find(key);
		if (found == chunks.end() || found->second.version == 0 || !found->second.ids) {
			continue;
		}
		const Chunk& chunk = found->second;
		if (biomeCopy.Empty() || biomeCopyKey != key || biomeCopyGeneration != chunk.generation) {
			biomeCopy.Resize(textureSize, textureSize);
			chunk.ids->Read(GL_RED_INTEGER, GL_INT, static_cast<int>(biomeCopy.Size() * sizeof(int)), biomeCopy.Data());
			biomeCopyKey = key;
			biomeCopyGeneration = chunk.generation;
		}
		const glm::ivec2 inChunk = sample - coord * chunkSize + 1;
		return biomeCopy.At(inChunk.x, inChunk.y);
	}
	return -1;
}
