#include "VegetationLayer.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>

#include "imgui.h"
#include "glm/gtc/matrix_transform.hpp"

#include "Camera.h"
#include "Frustum.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "VegetationModels.h"

namespace {
	//Instance as the vertex shader reads it (Instances in vegetation_vertex.shader)
	struct GpuInstance {
		glm::vec4 positionScale;
		glm::vec4 rotation;
	};
	//Groups per variant: the three levels of detail and the impostors (VegetationLayer::impostorLod)
	constexpr int lodCount = 4;

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
}

VegetationLayer::~VegetationLayer()
{
	if (instanceBuffer) {
		glDeleteBuffers(1, &instanceBuffer);
	}
}

void VegetationLayer::Update(const ChunkWorld& chunks, const VegetationModels* models, const World& world)
{
	if (!enabled || !models) {
		placed.clear();
		placedReadyModels = -1;
		return;
	}
	const std::vector<VegetationModels::Model>& list = models->GetModels();
	std::vector<vegetation::ModelInfo> infos;
	int ready = 0;
	for (const VegetationModels::Model& m : list) {
		const bool isReady = m.state == VegetationModels::State::READY;
		infos.push_back({ m.name, isReady ? static_cast<int>(m.variants.size()) : 0, vegetation::LayerOfKind(m.kind) });
		ready += isReady ? 1 : 0;
	}
	//Anything placement depends on changed: place every chunk again, nearest first, showing the old instances meanwhile
	if (ready != placedReadyModels || !SamePlants(world.biomes, placedPlants) || world.waterHeight != placedWater || world.seed != placedSeed
		|| density != placedDensity) {
		for (auto& [key, chunk] : placed) {
			chunk.stale = true;
		}
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
	const std::vector<ChunkWorld::FineChunk> fine = chunks.FineChunks(maxRange * vegetation::unitsPerMetre);

	//Forget chunks out of range or gone from the cache
	std::unordered_set<uint64_t> inRange;
	for (const ChunkWorld::FineChunk& c : fine) {
		inRange.insert(c.key);
	}
	for (auto it = placed.begin(); it != placed.end();) {
		it = inRange.count(it->first) ? std::next(it) : placed.erase(it);
	}

	const auto start = std::chrono::steady_clock::now();
	int placements = 0;
	std::vector<float> height(ChunkWorld::textureSize * ChunkWorld::textureSize);
	std::vector<glm::vec4> surface(height.size());
	std::vector<int> ids(height.size());
	unplaced = 0;
	for (const ChunkWorld::FineChunk& c : fine) {
		const auto found = placed.find(c.key);
		if (found != placed.end() && found->second.generation == c.generation && !found->second.stale) {
			continue;
		}
		if (placements >= maxPlacementsPerFrame) {
			unplaced++;
			continue;
		}
		ChunkInstances& chunk = placed[c.key];
		chunk.generation = c.generation;
		chunk.stale = false;
		chunk.instances.clear();
		//Without biomes there are no plants: the chunk stays empty
		if (!c.ids || !c.height || !c.surface || placedPlants.empty()) {
			continue;
		}
		placements++;
		c.height->Read(GL_RED, GL_FLOAT, static_cast<int>(height.size() * sizeof(float)), height.data());
		c.surface->Read(GL_RGBA, GL_FLOAT, static_cast<int>(surface.size() * sizeof(glm::vec4)), surface.data());
		c.ids->Read(GL_RED_INTEGER, GL_INT, static_cast<int>(ids.size() * sizeof(int)), ids.data());

		vegetation::ChunkMaps maps;
		maps.size = ChunkWorld::textureSize;
		maps.firstTexel = c.coord * ChunkWorld::chunkSize - 1;
		maps.height = height.data();
		maps.surface = surface.data();
		maps.ids = ids.data();
		vegetation::PlacementSettings settings;
		settings.chunkTexel = c.coord * ChunkWorld::chunkSize;
		settings.chunkTexels = ChunkWorld::chunkSize;
		settings.mapOrigin = chunks.GetMapOrigin();
		settings.worldUnitsPerTexel = TerrainView::worldUnitsPerTexel;
		settings.heightScale = world.heightScale;
		settings.waterHeight = world.waterHeight;
		settings.density = density;
		settings.seed = world.seed;
		vegetation::PlaceChunk(maps, placedPlants, infos, settings, chunk.instances);
	}
	if (placements > 0) {
		placementMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}
}

void VegetationLayer::Draw(Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, VegetationModels* models)
{
	drawnInstances = 0;
	drawnTriangles = 0;
	std::fill(std::begin(drawnPerLod), std::end(drawnPerLod), 0);
	if (!enabled || !models || settings.map2d || placed.empty()) {
		return;
	}
	const std::vector<VegetationModels::Model>& list = models->GetModels();
	//Group index of (model, variant, lod)
	std::vector<int> firstGroup(list.size() + 1, 0);
	for (size_t m = 0; m < list.size(); m++) {
		const int variants = list[m].state == VegetationModels::State::READY ? static_cast<int>(list[m].variants.size()) : 0;
		firstGroup[m + 1] = firstGroup[m] + variants * lodCount;
	}
	std::vector<std::vector<GpuInstance>> groups(static_cast<size_t>(firstGroup.back()));

	const glm::vec3 eye = camera.GetPosition() / settings.modelScale;
	const Frustum frustum(camera.GetViewProjection() * glm::scale(glm::mat4(1.0f), glm::vec3(settings.modelScale)));
	float rangeSquared[vegetation::layerCount][lodCount];
	for (int l = 0; l < vegetation::layerCount; l++) {
		const float units = vegetation::unitsPerMetre;
		rangeSquared[l][0] = ranges[l].lod1 * ranges[l].lod1 * units * units;
		rangeSquared[l][1] = ranges[l].lod2 * ranges[l].lod2 * units * units;
		rangeSquared[l][2] = ranges[l].impostor * ranges[l].impostor * units * units;
		rangeSquared[l][3] = ranges[l].max * ranges[l].max * units * units;
	}
	for (const auto& [key, chunk] : placed) {
		for (const vegetation::Instance& i : chunk.instances) {
			if (i.model >= static_cast<int>(list.size()) || list[i.model].state != VegetationModels::State::READY
				|| i.variant >= static_cast<int>(list[i.model].variants.size())) {
				continue;
			}
			const glm::vec3 base(i.position.x, i.position.y * settings.heightScale, i.position.z);
			const glm::vec3 offset = base - eye;
			const float distanceSquared = glm::dot(offset, offset);
			const int layer = static_cast<int>(i.layer);
			if (distanceSquared > rangeSquared[layer][3]) {
				continue;
			}
			//The box of the variant turned any way: its largest horizontal extent around the base
			const vegetation::Variant& variant = list[i.model].variants[i.variant];
			const float reach = std::max({ std::abs(variant.boundsMin.x), std::abs(variant.boundsMax.x), std::abs(variant.boundsMin.z),
				std::abs(variant.boundsMax.z) }) * i.scale;
			if (!frustum.Intersects(base + glm::vec3(-reach, variant.boundsMin.y * i.scale, -reach),
				base + glm::vec3(reach, variant.boundsMax.y * i.scale, reach))) {
				continue;
			}
			int lod = 0;
			while (lod < impostorLod && distanceSquared >= rangeSquared[layer][lod]) {
				lod++;
			}
			int usedLod = lod;
			if (lod == impostorLod && (!impostors || !models->HasImpostors(i.model))) {
				usedLod = impostorLod - 1;
			}
			if (usedLod < impostorLod) {
				usedLod = std::min(usedLod, static_cast<int>(variant.lods.size()) - 1);
				if (usedLod < 0) {
					continue;
				}
			}
			groups[static_cast<size_t>(firstGroup[i.model] + i.variant * lodCount + usedLod)].push_back(
				{ glm::vec4(i.position, i.scale), glm::vec4(i.yaw, 0.0f, 0.0f, 0.0f) });
		}
	}

	std::vector<GpuInstance> all;
	for (const std::vector<GpuInstance>& g : groups) {
		all.insert(all.end(), g.begin(), g.end());
	}
	if (all.empty()) {
		return;
	}
	if (!instanceBuffer) {
		glCreateBuffers(1, &instanceBuffer);
	}
	if (all.size() > instanceCapacity) {
		instanceCapacity = all.size() + all.size() / 2;
		glNamedBufferData(instanceBuffer, static_cast<GLsizeiptr>(instanceCapacity * sizeof(GpuInstance)), nullptr, GL_DYNAMIC_DRAW);
	}
	glNamedBufferSubData(instanceBuffer, 0, static_cast<GLsizeiptr>(all.size() * sizeof(GpuInstance)), all.data());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, VegetationModels::instanceBinding, instanceBuffer);

	//Leaves are two-sided
	const GLboolean culling = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);
	if (settings.wireFrame) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(settings.modelScale));
	const bool fogOn = light.IsFogOn();
	const float fogStart = light.GetFogStart(camera.GetRenderDistance()), fogDistance = camera.GetRenderDistance();
	//Meshes first, then impostors with their own shader; the instances of a group start where the groups before it end
	for (int pass = 0; pass < 2; pass++) {
		const bool impostorPass = pass == 1;
		if (impostorPass) {
			models->BeginImpostorDraw(*camera.GetViewMatrix(), *camera.GetProjectionMatrix(), camera.GetPosition(), scale, eye, settings.heightScale,
				light, fogOn, fogStart, fogDistance);
		}
		else {
			models->BeginDraw(*camera.GetViewMatrix(), *camera.GetProjectionMatrix(), camera.GetPosition(), scale, settings.heightScale, light,
				fogOn, fogStart, fogDistance);
		}
		int first = 0;
		for (size_t m = 0; m < list.size(); m++) {
			for (int g = firstGroup[m]; g < firstGroup[m + 1]; g++) {
				const int count = static_cast<int>(groups[static_cast<size_t>(g)].size());
				const int variant = (g - firstGroup[m]) / lodCount, lod = (g - firstGroup[m]) % lodCount;
				if (count == 0 || (lod == impostorLod) != impostorPass) {
					first += count;
					continue;
				}
				if (impostorPass) {
					models->DrawImpostors(static_cast<int>(m), variant, first, count);
					drawnTriangles += 2ull * static_cast<uint64_t>(count);
				}
				else {
					models->Draw(static_cast<int>(m), variant, lod, first, count);
					drawnTriangles += static_cast<uint64_t>(list[m].variants[variant].lods[lod].triangles) * static_cast<uint64_t>(count);
				}
				first += count;
				drawnInstances += count;
				drawnPerLod[lod] += count;
			}
		}
	}
	if (settings.wireFrame) {
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
		total += chunk.instances.size();
	}
	return total;
}

void VegetationLayer::ImGuiDraw()
{
	ImGui::Checkbox("Show in the infinite world", &enabled);
	ImGui::SetItemTooltip("Trees, shrubs and rocks on chunks of full detail; needs biomes (species are picked per biome)");
	ImGui::SliderFloat("Density", &density, 0.0f, 3.0f);
	ImGui::SetItemTooltip("Multiplies the chance of every species; a change places everything again");
	ImGui::Checkbox("Impostors", &impostors);
	ImGui::SetItemTooltip("Distant trees as camera-facing pictures baked from 64 directions; off = LOD 2 meshes up to the range");
	if (ImGui::TreeNode("Distances (m)")) {
		for (int l = 0; l < vegetation::layerCount; l++) {
			ImGui::PushID(l);
			ImGui::TextUnformatted(vegetation::layerNames[l]);
			LayerRange& r = ranges[l];
			ImGui::DragFloat("LOD 1 from", &r.lod1, 1.0f, 1.0f, r.lod2);
			ImGui::DragFloat("LOD 2 from", &r.lod2, 1.0f, r.lod1, r.impostor);
			ImGui::DragFloat("Impostors from", &r.impostor, 1.0f, r.lod2, r.max);
			ImGui::SetItemTooltip("Only models with impostors (trees); others keep LOD 2 up to the range");
			ImGui::DragFloat("Range", &r.max, 2.0f, r.impostor, 2000.0f);
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
	ImGui::Text("Placed: %zu on %zu chunks (last placement %.2f ms)", PlacedCount(), placed.size(), placementMs);
	ImGui::Text("Drawn: %d (LOD 0/1/2: %d / %d / %d, impostors %d)", drawnInstances, drawnPerLod[0], drawnPerLod[1], drawnPerLod[2],
		drawnPerLod[impostorLod]);
	ImGui::Text("Triangles: %.2f M", static_cast<double>(drawnTriangles) / 1e6);
}
