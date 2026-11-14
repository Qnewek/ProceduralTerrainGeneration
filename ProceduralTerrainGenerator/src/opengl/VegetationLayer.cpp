#include "VegetationLayer.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <unordered_set>

#include "imgui.h"
#include "glm/gtc/matrix_transform.hpp"

#include "Camera.h"
#include "Frustum.h"
#include "LightSource.h"
#include "Shader.h"
#include "TerrainView.h"
#include "VegetationModels.h"

namespace {
	using GpuInstance = VegetationModels::GpuInstance;
	//Groups per variant: the three levels of detail and the impostors (VegetationLayer::impostorLod)
	constexpr int lodsPerVariant = 4;
	//Heights of the maps stay within +-heightBound (ChunkWorld.cpp); the tallest plants and their reach beyond the chunk, in metres
	constexpr float heightBound = 1.5f;
	constexpr float plantHeight = 60.0f, plantReach = 20.0f;

	//true if the plants of the table are the ones placed (an empty table = biomes off)
	bool SamePlants(const std::vector<biome::Biome>* biomes, const std::vector<std::vector<biome::Plant>>& placed)
	{
		const size_t count = biomes ? biomes->size() : 0;
		if (count != placed.size()) {
			return false;
		}
		for (size_t i = 0; i < count; i++) {
			if ((*biomes)[i].plants != placed[i]) {
				return false;
			}
		}
		return true;
	}

	//Replaces the contents of a buffer, creating it if needed
	void Upload(GLuint& buffer, const void* data, size_t bytes)
	{
		if (!buffer) {
			glCreateBuffers(1, &buffer);
		}
		glNamedBufferData(buffer, static_cast<GLsizeiptr>(std::max<size_t>(bytes, 16)), data, GL_DYNAMIC_DRAW);
	}
}

void VegetationLayer::Buffer::Reserve(size_t needed)
{
	if (!id) {
		glCreateBuffers(1, &id);
	}
	if (needed > bytes) {
		bytes = std::max<size_t>(needed + needed / 2, 1024);
		glNamedBufferData(id, static_cast<GLsizeiptr>(bytes), nullptr, GL_DYNAMIC_DRAW);
	}
}

VegetationLayer::VegetationLayer() = default;

VegetationLayer::~VegetationLayer()
{
	const GLuint buffers[] = { placedBuffer, rangesBuffer.id, classifiedBuffer.id, drawnBuffer.id, countsBuffer, offsetsBuffer, statsBuffer,
		variantsBuffer, modelsBuffer, meshCommandBuffer, meshGroupBuffer, impostorCommandBuffer, impostorGroupBuffer };
	for (GLuint b : buffers) {
		if (b) {
			glDeleteBuffers(1, &b);
		}
	}
}

bool VegetationLayer::ReloadShader()
{
	return !cullShader || cullShader->Reload();
}

void VegetationLayer::Update(const ChunkWorld& chunks, const VegetationModels* models, const World& world)
{
	if (!enabled || !models) {
		for (auto& [key, chunk] : placed) {
			Release(chunk);
		}
		placed.clear();
		ground.clear();
		drawRanges.clear();
		placedReadyModels = -1;
		return;
	}
	const std::vector<VegetationModels::Model>& list = models->GetModels();
	int ready = 0;
	for (const VegetationModels::Model& m : list) {
		ready += m.state == VegetationModels::State::READY ? 1 : 0;
	}
	if (ready != tablesReadyModels) {
		BuildTables(*models);
	}
	//Anything placement depends on changed: place every chunk again, nearest first, showing the old instances meanwhile
	if (ready != placedReadyModels || !SamePlants(world.biomes, placedPlants) || world.waterHeight != placedWater || world.seed != placedSeed
		|| density != placedDensity) {
		for (auto& [key, chunk] : placed) {
			chunk.stale = true;
		}
		settingsVersion++;
		placedReadyModels = ready;
		placedPlants.clear();
		if (world.biomes) {
			for (const biome::Biome& b : *world.biomes) {
				placedPlants.push_back(b.plants);
			}
		}
		placedWater = world.waterHeight;
		placedSeed = world.seed;
		placedDensity = density;
	}

	float maxRange = 0.0f;
	for (const LayerRange& r : ranges) {
		maxRange = std::max(maxRange, r.max);
	}
	ground = chunks.GroundChunks(ChunkWorld::maxLevel, maxRange * vegetation::unitsPerMetre);

	//Finished placements go to the GPU
	for (auto it = placing.begin(); it != placing.end();) {
		std::optional<PlacementResult> result = it->job->TryTake();
		if (!result) {
			++it;
			continue;
		}
		Install(*it, *result);
		it = placing.erase(it);
	}

	int readbacks = 0;
	unplaced = 0;
	for (const ChunkWorld::GroundChunk& g : ground) {
		const auto found = placed.find(g.key);
		if (found != placed.end() && found->second.generation == g.generation && !found->second.stale) {
			continue;
		}
		unplaced++;
		//One placement per chunk at a time; a newer one starts once it is installed
		const bool running = std::any_of(placing.begin(), placing.end(), [&](const Placing& p) { return p.key == g.key; });
		if (running || readbacks >= maxPlacementsPerFrame || static_cast<int>(placing.size()) >= maxPlacingJobs) {
			continue;
		}
		StartPlacement(chunks, g, world);
		readbacks++;
	}
	CollectRanges(chunks);
}

void VegetationLayer::Install(const Placing& p, PlacementResult& result)
{
	ChunkInstances& chunk = placed[p.key];
	Release(chunk);
	chunk.generation = p.generation;
	chunk.stale = p.settingsVersion != settingsVersion;
	chunk.level = p.level;
	chunk.coord = p.coord;
	chunk.blockStarts = std::move(result.blockStarts);
	placementMs = result.ms;
	std::vector<GpuInstance> gpu(result.instances.size());
	for (size_t i = 0; i < result.instances.size(); i++) {
		const vegetation::Instance& in = result.instances[i];
		gpu[i] = { glm::vec4(in.position, in.scale),
			glm::vec4(in.yaw, static_cast<float>(in.variant), static_cast<float>(in.model), static_cast<float>(static_cast<int>(in.layer))) };
	}
	chunk.count = static_cast<uint32_t>(gpu.size());
	chunk.offset = Allocate(chunk.count);
	if (!gpu.empty()) {
		glNamedBufferSubData(placedBuffer, static_cast<GLintptr>(chunk.offset) * sizeof(GpuInstance),
			static_cast<GLsizeiptr>(gpu.size() * sizeof(GpuInstance)), gpu.data());
	}
}

void VegetationLayer::StartPlacement(const ChunkWorld& chunks, const ChunkWorld::GroundChunk& g, const World& world)
{
	Placing p;
	p.key = g.key;
	p.generation = g.generation;
	p.settingsVersion = settingsVersion;
	p.level = g.level;
	p.coord = g.coord;
	p.job = std::make_unique<GenerationJob<PlacementResult>>();
	const int blocksPerSide = 1 << g.level;
	//Without biomes there are no plants: the chunk stays empty
	if (!g.ids || !g.height || !g.surface || placedPlants.empty()) {
		p.job->Start([blocksPerSide]() {
			PlacementResult r;
			r.blockStarts.assign(static_cast<size_t>(blocksPerSide) * blocksPerSide + 1, 0);
			return r;
		});
		placing.push_back(std::move(p));
		return;
	}
	const size_t texels = static_cast<size_t>(ChunkWorld::textureSize) * ChunkWorld::textureSize;
	std::vector<float> height(texels);
	std::vector<glm::vec4> surface(texels);
	std::vector<int> ids(texels);
	g.height->Read(GL_RED, GL_FLOAT, static_cast<int>(height.size() * sizeof(float)), height.data());
	g.surface->Read(GL_RGBA, GL_FLOAT, static_cast<int>(surface.size() * sizeof(glm::vec4)), surface.data());
	g.ids->Read(GL_RED_INTEGER, GL_INT, static_cast<int>(ids.size() * sizeof(int)), ids.data());

	const int step = 1 << g.level;
	vegetation::PlacementSettings settings;
	settings.chunkTexel = g.coord * ChunkWorld::chunkSize * step;
	settings.chunkTexels = ChunkWorld::chunkSize * step;
	settings.mapOrigin = chunks.GetMapOrigin();
	settings.worldUnitsPerTexel = TerrainView::worldUnitsPerTexel;
	settings.heightScale = world.heightScale;
	settings.waterHeight = world.waterHeight;
	settings.density = density;
	settings.seed = world.seed;
	//Far away only trees: shrubs and rocks are never drawn that far
	if (g.level > 0) {
		settings.layerMask = 1u << static_cast<int>(vegetation::Layer::TREES);
	}

	//The worker owns copies of everything it reads
	p.job->Start([height = std::move(height), surface = std::move(surface), ids = std::move(ids), plants = placedPlants, infos = modelInfos,
		settings, step, firstTexel = g.coord * ChunkWorld::chunkSize * step - step, blocksPerSide]() {
		const auto start = std::chrono::steady_clock::now();
		vegetation::ChunkMaps maps;
		maps.size = ChunkWorld::textureSize;
		maps.firstTexel = firstTexel;
		maps.step = step;
		maps.height = height.data();
		maps.surface = surface.data();
		maps.ids = ids.data();
		PlacementResult r;
		vegetation::PlaceChunk(maps, plants, infos, settings, r.instances);
		r.blockStarts = vegetation::SortByBlock(r.instances, settings.mapOrigin, settings.worldUnitsPerTexel, settings.chunkTexel,
			ChunkWorld::chunkSize, blocksPerSide);
		r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return r;
	});
	placing.push_back(std::move(p));
}

void VegetationLayer::Release(ChunkInstances& chunk)
{
	allocator.Free(chunk.offset, chunk.count);
	chunk.offset = 0;
	chunk.count = 0;
}

uint32_t VegetationLayer::Allocate(uint32_t count)
{
	std::optional<uint32_t> range = allocator.Allocate(count);
	if (range) {
		return *range;
	}
	//Grow: a new buffer with the old contents at the same places
	const uint32_t oldCapacity = allocator.Capacity();
	const uint32_t capacity = std::max(oldCapacity * 2, oldCapacity + count + 65536u);
	GLuint grown = 0;
	glCreateBuffers(1, &grown);
	glNamedBufferStorage(grown, static_cast<GLsizeiptr>(capacity) * sizeof(GpuInstance), nullptr, GL_DYNAMIC_STORAGE_BIT);
	if (placedBuffer) {
		glCopyNamedBufferSubData(placedBuffer, grown, 0, 0, static_cast<GLsizeiptr>(oldCapacity) * sizeof(GpuInstance));
		glDeleteBuffers(1, &placedBuffer);
	}
	placedBuffer = grown;
	allocator.Grow(capacity);
	return *allocator.Allocate(count);
}

void VegetationLayer::CollectRanges(const ChunkWorld& chunks)
{
	drawRanges.clear();
	std::unordered_set<uint64_t> keep;
	const glm::vec2 mapOrigin = chunks.GetMapOrigin();
	auto area = [&](int level, glm::ivec2 coord, DrawRange& range) {
		const float size = static_cast<float>(ChunkWorld::chunkSize << level) * TerrainView::worldUnitsPerTexel;
		range.areaMin = mapOrigin + glm::vec2(coord) * size;
		range.areaMax = range.areaMin + size;
	};
	//Placed finer chunks within a chunk not placed yet (the camera moved away and the ground got coarser)
	std::function<void(int, glm::ivec2)> descend = [&](int level, glm::ivec2 coord) {
		if (level < 0) {
			return;
		}
		const auto found = placed.find(ChunkWorld::Key(level, coord));
		if (found != placed.end()) {
			keep.insert(found->first);
			DrawRange r{ found->second.offset, found->second.count };
			area(level, coord, r);
			drawRanges.push_back(r);
			return;
		}
		for (int y = 0; y < 2; y++) {
			for (int x = 0; x < 2; x++) {
				descend(level - 1, coord * 2 + glm::ivec2(x, y));
			}
		}
	};

	for (const ChunkWorld::GroundChunk& g : ground) {
		const auto own = placed.find(g.key);
		if (own != placed.end()) {
			keep.insert(g.key);
			DrawRange r{ own->second.offset, own->second.count };
			area(g.level, g.coord, r);
			drawRanges.push_back(r);
			continue;
		}
		//The part of the nearest placed coarser chunk over it: an aligned square of blocks, contiguous in Morton order
		bool covered = false;
		for (int level = g.level + 1; level <= ChunkWorld::maxLevel && !covered; level++) {
			const glm::ivec2 coord(g.coord.x >> (level - g.level), g.coord.y >> (level - g.level));
			const auto found = placed.find(ChunkWorld::Key(level, coord));
			if (found == placed.end()) {
				continue;
			}
			const ChunkInstances& coarse = found->second;
			const glm::ivec2 relative = g.coord * (1 << g.level) - coord * (1 << level);
			const uint32_t side = 1u << g.level;
			const uint32_t first = vegetation::Morton(static_cast<uint32_t>(relative.x), static_cast<uint32_t>(relative.y));
			const uint32_t last = first + side * side;
			if (last < coarse.blockStarts.size()) {
				DrawRange r{ coarse.offset + coarse.blockStarts[first], coarse.blockStarts[last] - coarse.blockStarts[first] };
				area(g.level, g.coord, r);
				drawRanges.push_back(r);
			}
			keep.insert(found->first);
			covered = true;
		}
		if (!covered) {
			descend(g.level - 1, g.coord * 2);
			descend(g.level - 1, g.coord * 2 + glm::ivec2(1, 0));
			descend(g.level - 1, g.coord * 2 + glm::ivec2(0, 1));
			descend(g.level - 1, g.coord * 2 + glm::ivec2(1, 1));
		}
	}
	for (auto it = placed.begin(); it != placed.end();) {
		if (keep.count(it->first)) {
			++it;
		}
		else {
			Release(it->second);
			it = placed.erase(it);
		}
	}
}

void VegetationLayer::BuildTables(const VegetationModels& models)
{
	const std::vector<VegetationModels::Model>& list = models.GetModels();
	std::vector<glm::uvec4> infos(std::max<size_t>(list.size(), 1), glm::uvec4(0));
	std::vector<glm::vec4> variants;
	std::vector<GLuint> meshCommands, meshGroups, impostorCommands, impostorGroups;
	groupTriangles.clear();
	groupLod.clear();
	meshCommandFirst.assign(list.size(), -1);
	impostorCommandFirst.assign(list.size(), -1);
	modelInfos.clear();
	int groups = 0, ready = 0;
	for (size_t m = 0; m < list.size(); m++) {
		const VegetationModels::Model& model = list[m];
		const bool isReady = model.state == VegetationModels::State::READY && !model.variants.empty();
		modelInfos.push_back({ model.name, isReady ? static_cast<int>(model.variants.size()) : 0, vegetation::LayerOfKind(model.kind) });
		ready += model.state == VegetationModels::State::READY ? 1 : 0;
		if (!isReady) {
			continue;
		}
		const bool hasImpostors = models.HasImpostors(static_cast<int>(m));
		infos[m] = glm::uvec4(static_cast<GLuint>(variants.size()), static_cast<GLuint>(groups), hasImpostors ? 1u : 0u, 0u);
		for (const vegetation::Variant& v : model.variants) {
			const float reach = std::max({ std::abs(v.boundsMin.x), std::abs(v.boundsMax.x), std::abs(v.boundsMin.z), std::abs(v.boundsMax.z) });
			const int lods = static_cast<int>(v.lods.size());
			variants.push_back(glm::vec4(reach, v.boundsMin.y, v.boundsMax.y, static_cast<float>(std::max(lods - 1, 0))));
			for (int lod = 0; lod < lodsPerVariant; lod++) {
				groupLod.push_back(lod);
				groupTriangles.push_back(lod == impostorLod ? 2u : lod < lods ? v.lods[lod].triangles : 0u);
			}
		}
		meshCommandFirst[m] = static_cast<int>(meshGroups.size());
		for (const VegetationModels::MeshCommand& c : model.commands) {
			meshCommands.insert(meshCommands.end(), { c.indexCount, 0u, c.firstIndex, 0u, 0u });
			meshGroups.push_back(static_cast<GLuint>(groups + c.variant * lodsPerVariant + c.lod));
		}
		if (hasImpostors) {
			impostorCommandFirst[m] = static_cast<int>(impostorGroups.size());
			for (int v = 0; v < static_cast<int>(model.variants.size()); v++) {
				impostorCommands.insert(impostorCommands.end(), { 4u, 0u, 0u, 0u });
				impostorGroups.push_back(static_cast<GLuint>(groups + v * lodsPerVariant + impostorLod));
			}
		}
		groups += static_cast<int>(model.variants.size()) * lodsPerVariant;
	}
	groupCount = groups;
	meshCommandCount = static_cast<int>(meshGroups.size());
	impostorCommandCount = static_cast<int>(impostorGroups.size());
	Upload(modelsBuffer, infos.data(), infos.size() * sizeof(glm::uvec4));
	Upload(variantsBuffer, variants.data(), variants.size() * sizeof(glm::vec4));
	Upload(meshCommandBuffer, meshCommands.data(), meshCommands.size() * sizeof(GLuint));
	Upload(meshGroupBuffer, meshGroups.data(), meshGroups.size() * sizeof(GLuint));
	Upload(impostorCommandBuffer, impostorCommands.data(), impostorCommands.size() * sizeof(GLuint));
	Upload(impostorGroupBuffer, impostorGroups.data(), impostorGroups.size() * sizeof(GLuint));
	Upload(countsBuffer, nullptr, static_cast<size_t>(groups) * sizeof(GLuint));
	Upload(offsetsBuffer, nullptr, static_cast<size_t>(groups) * sizeof(GLuint));
	Upload(statsBuffer, nullptr, static_cast<size_t>(groups) * sizeof(GLuint));
	statsPending = false;
	tablesReadyModels = ready;
}

void VegetationLayer::Draw(Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, VegetationModels* models)
{
	//Counts of the last frame, copied after its culling: reading them now hardly waits
	if (statsPending) {
		std::vector<GLuint> counts(static_cast<size_t>(groupCount));
		glGetNamedBufferSubData(statsBuffer, 0, static_cast<GLsizeiptr>(counts.size() * sizeof(GLuint)), counts.data());
		drawnInstances = 0;
		drawnTriangles = 0;
		std::fill(std::begin(drawnPerLod), std::end(drawnPerLod), 0);
		for (int g = 0; g < groupCount; g++) {
			drawnInstances += static_cast<int>(counts[g]);
			drawnPerLod[groupLod[g]] += static_cast<int>(counts[g]);
			drawnTriangles += groupTriangles[g] * counts[g];
		}
		statsPending = false;
	}
	if (!enabled || !models || settings.map2d || drawRanges.empty() || groupCount == 0) {
		drawnInstances = 0;
		drawnTriangles = 0;
		std::fill(std::begin(drawnPerLod), std::end(drawnPerLod), 0);
		return;
	}
	const glm::vec3 eye = camera.GetPosition() / settings.modelScale;
	if (!Cull(eye, camera.GetViewProjection(), settings, detailScale, 0.0f, true)) {
		drawnInstances = 0;
		drawnTriangles = 0;
		std::fill(std::begin(drawnPerLod), std::end(drawnPerLod), 0);
		return;
	}
	DrawCulled(TerrainView::FrameBlock(camera, light, settings, glm::vec2(0.0f), glm::vec2(0.0f)), eye, settings, *models, settings.wireFrame);
}

bool VegetationLayer::CullShadows(Camera& camera, const glm::mat4& sunViewProjection, const TerrainRenderSettings& settings, VegetationModels* models)
{
	if (!enabled || !shadows || !models || settings.map2d || drawRanges.empty() || groupCount == 0) {
		return false;
	}
	//Simpler models than the camera sees: a shadow hardly shows the difference and every cascade draws them
	return Cull(camera.GetPosition() / settings.modelScale, sunViewProjection, settings, detailScale * shadowDetail, shadowSmallRange, false);
}

void VegetationLayer::DrawShadow(const FrameUniforms::Block& block, Camera& camera, const TerrainRenderSettings& settings, VegetationModels& models)
{
	//Impostors turn to the sun: the eye far away towards it
	const glm::vec3 eye = camera.GetPosition() / settings.modelScale + glm::vec3(block.lightDirection) * 1.0e6f;
	DrawCulled(block, eye, settings, models, false);
}

bool VegetationLayer::Cull(const glm::vec3& eye, const glm::mat4& viewProjection, const TerrainRenderSettings& settings, float lodScale, float smallRange, bool stats)
{
	if (!cullShader) {
		cullShader = Shader::Compute("res/shaders/VegetationShaders/cull_compute.shader");
	}
	if (!cullShader || !cullShader->IsValid()) {
		return false;
	}

	//Ranges of chunks in the frustum and in range
	const Frustum frustum(viewProjection * glm::scale(glm::mat4(1.0f), glm::vec3(settings.modelScale)));
	const float units = vegetation::unitsPerMetre;
	float maxRange = 0.0f;
	for (const LayerRange& r : ranges) {
		maxRange = std::max(maxRange, r.max * units);
	}
	const float scale = std::abs(settings.heightScale);
	std::vector<glm::uvec4> rangeData;
	uint32_t total = 0;
	for (const DrawRange& r : drawRanges) {
		if (r.count == 0) {
			continue;
		}
		const glm::vec2 low = r.areaMin - plantReach * units, high = r.areaMax + plantReach * units;
		const glm::vec2 nearest = glm::clamp(glm::vec2(eye.x, eye.z), low, high);
		if (glm::length(nearest - glm::vec2(eye.x, eye.z)) > maxRange
			|| !frustum.Intersects(glm::vec3(low.x, -heightBound * scale, low.y), glm::vec3(high.x, heightBound * scale + plantHeight * units, high.y))) {
			continue;
		}
		rangeData.push_back(glm::uvec4(r.first, r.count, total, 0u));
		total += r.count;
	}
	if (rangeData.empty()) {
		return false;
	}
	rangesBuffer.Reserve(rangeData.size() * sizeof(glm::uvec4));
	classifiedBuffer.Reserve(static_cast<size_t>(total) * sizeof(GLuint));
	drawnBuffer.Reserve(static_cast<size_t>(total) * sizeof(GpuInstance));
	//The buffers may still be read by the draws of a pass before (shadows)
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
	glNamedBufferSubData(rangesBuffer.id, 0, static_cast<GLsizeiptr>(rangeData.size() * sizeof(glm::uvec4)), rangeData.data());
	glClearNamedBufferData(countsBuffer, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

	const GLuint bindings[] = { placedBuffer, rangesBuffer.id, classifiedBuffer.id, countsBuffer, variantsBuffer, modelsBuffer, offsetsBuffer,
		meshCommandBuffer, meshGroupBuffer, impostorCommandBuffer, impostorGroupBuffer, drawnBuffer.id };
	for (GLuint b = 0; b < static_cast<GLuint>(std::size(bindings)); b++) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, b, bindings[b]);
	}
	cullShader->Bind();
	cullShader->SetUniform3fv("eye", eye);
	glUniform4fv(glGetUniformLocation(cullShader->GetRendererID(), "planes"), 6, &frustum.Planes()[0].x);
	cullShader->SetUniform1f("heightScale", settings.heightScale);
	glm::vec4 squared[vegetation::layerCount];
	for (int l = 0; l < vegetation::layerCount; l++) {
		const LayerRange& r = ranges[l];
		const float range = l != static_cast<int>(vegetation::Layer::TREES) && smallRange > 0.0f ? std::min(r.max, smallRange) : r.max;
		//Scaled detail distances stay within the range
		const glm::vec3 detail = glm::min(glm::vec3(r.lod1, r.lod2, r.impostor) * lodScale, glm::vec3(range));
		squared[l] = glm::vec4(detail, range) * units;
		squared[l] *= squared[l];
	}
	glUniform4fv(glGetUniformLocation(cullShader->GetRendererID(), "rangeSquared"), vegetation::layerCount, &squared[0].x);
	cullShader->SetUniform1i("impostorsOn", impostors ? 1 : 0);
	cullShader->SetUniform1i("groupCount", groupCount);
	cullShader->SetUniform1i("meshCommandCount", meshCommandCount);
	cullShader->SetUniform1i("impostorCommandCount", impostorCommandCount);
	const GLuint rangeCount = static_cast<GLuint>(rangeData.size());
	cullShader->SetUniform1i("pass", 0);
	glDispatchCompute(rangeCount, 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	cullShader->SetUniform1i("pass", 1);
	glDispatchCompute(1, 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	cullShader->SetUniform1i("pass", 2);
	glDispatchCompute(rangeCount, 1, 1);
	glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
	if (stats) {
		glCopyNamedBufferSubData(countsBuffer, statsBuffer, 0, 0, static_cast<GLsizeiptr>(groupCount) * sizeof(GLuint));
		statsPending = true;
	}
	return true;
}

void VegetationLayer::DrawCulled(const FrameUniforms::Block& frame, const glm::vec3& eye, const TerrainRenderSettings& settings, VegetationModels& models,
	bool wireFrame)
{
	//Leaves are two-sided
	const GLboolean culling = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);
	if (wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	const glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(settings.modelScale));
	models.SetInstanceBuffer(drawnBuffer.id);
	const int modelCount = static_cast<int>(meshCommandFirst.size());

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, meshCommandBuffer);
	models.BeginDraw(frame, model, settings.heightScale);
	for (int m = 0; m < modelCount; m++) {
		if (meshCommandFirst[m] >= 0) {
			models.DrawMeshesIndirect(m, meshCommandFirst[m]);
		}
	}
	if (impostorCommandCount > 0) {
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, impostorCommandBuffer);
		models.BeginImpostorDraw(frame, model, eye, settings.heightScale);
		for (int m = 0; m < modelCount; m++) {
			if (impostorCommandFirst[m] >= 0) {
				models.DrawImpostorsIndirect(m, impostorCommandFirst[m]);
			}
		}
	}
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	if (wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	if (culling) {
		glEnable(GL_CULL_FACE);
	}
}

size_t VegetationLayer::PlacedCount() const
{
	size_t total = 0;
	for (const auto& [key, chunk] : placed) {
		total += chunk.count;
	}
	return total;
}

void VegetationLayer::ImGuiDraw()
{
	ImGui::Checkbox("Show in the infinite world", &enabled);
	ImGui::SetItemTooltip("Trees, shrubs and rocks on the ground near the camera, trees also far away; needs biomes (species are picked per biome)");
	ImGui::SliderFloat("Density", &density, 0.0f, 3.0f);
	ImGui::SetItemTooltip("Multiplies the chance of every species; a change places everything again");
	ImGui::SliderFloat("Detail distance", &detailScale, 0.25f, 5.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
	ImGui::SetItemTooltip("Multiplies the distances below (LOD 1, LOD 2, impostors) of all layers; trees get full models up to %.0f m.\n"
		"Full tree models have about 150k triangles each, so larger values cost GPU time quickly", ranges[0].lod1 * detailScale);
	ImGui::Checkbox("Impostors", &impostors);
	ImGui::SetItemTooltip("Distant trees as camera-facing pictures baked from 64 directions; off = LOD 2 meshes up to the range");
	ImGui::Checkbox("Plant shadows", &shadows);
	ImGui::SetItemTooltip("Plants cast sun shadows (Display > Shadows); every cascade draws them again");
	if (ImGui::TreeNode("Distances (m)")) {
		for (int l = 0; l < vegetation::layerCount; l++) {
			ImGui::PushID(l);
			ImGui::TextUnformatted(vegetation::layerNames[l]);
			LayerRange& r = ranges[l];
			ImGui::DragFloat("LOD 1 from", &r.lod1, 1.0f, 1.0f, r.lod2);
			ImGui::DragFloat("LOD 2 from", &r.lod2, 1.0f, r.lod1, r.impostor);
			ImGui::DragFloat("Impostors from", &r.impostor, 1.0f, r.lod2, r.max);
			ImGui::SetItemTooltip("Only models with impostors (trees); others keep LOD 2 up to the range");
			ImGui::DragFloat("Range", &r.max, 2.0f, r.impostor, 10000.0f);
			if (l == static_cast<int>(vegetation::Layer::TREES)) {
				ImGui::SetItemTooltip("Beyond the chunks of full detail trees stand on coarser chunks; the render distance limits it too");
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
	ImGui::Text("Placed: %zu on %zu chunks (last placement %.2f ms)", PlacedCount(), placed.size(), placementMs);
	ImGui::Text("Drawn: %d (LOD 0/1/2: %d / %d / %d, impostors %d)", drawnInstances, drawnPerLod[0], drawnPerLod[1], drawnPerLod[2],
		drawnPerLod[impostorLod]);
	ImGui::Text("Triangles: %.2f M", static_cast<double>(drawnTriangles) / 1e6);
}
