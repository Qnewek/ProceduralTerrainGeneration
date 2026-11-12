#include "VegetationModels.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>
#include "imgui.h"
#include "stb_image.h"
#include "glm/gtc/matrix_transform.hpp"

#include "LightSource.h"

namespace {
	//Values of a map the material does not have
	constexpr unsigned char flatNormal = 128;
	constexpr unsigned char defaultRoughness = 220;
	constexpr unsigned char noOcclusion = 255;
	//Models loaded at the same time; a large tree takes a few hundred MB while its levels are built
	constexpr unsigned int maxWorkers = 4;
	//Binding of the instance buffer (Instances in vegetation_vertex.shader)
	constexpr GLuint instanceBinding = 1;
	constexpr int albedoUnit = 7;
	constexpr int detailUnit = 8;
	constexpr int previewSize = 512;

	struct Image {
		int width = 0, height = 0;
		std::vector<unsigned char> pixels;
	};

	//Loads an image with the given number of channels, rows as stored: glTF texture coordinates start at the top-left corner
	bool LoadImage(const std::filesystem::path& path, int channels, Image& image)
	{
		if (!std::filesystem::exists(path)) {
			return false;
		}
		stbi_set_flip_vertically_on_load_thread(0);
		unsigned char* data = stbi_load(path.string().c_str(), &image.width, &image.height, nullptr, channels);
		if (!data) {
			std::cout << "[ERROR] VegetationModels: cannot decode " << path.string() << ": " << stbi_failure_reason() << std::endl;
			return false;
		}
		image.pixels.assign(data, data + static_cast<size_t>(image.width) * image.height * channels);
		stbi_image_free(data);
		return true;
	}

	//Bilinear copy of an image at another size
	Image Resample(const Image& source, int channels, int width, int height)
	{
		if (source.width == width && source.height == height) {
			return source;
		}
		Image result;
		result.width = width;
		result.height = height;
		result.pixels.resize(static_cast<size_t>(width) * height * channels);
		for (int y = 0; y < height; y++) {
			const float sy = std::clamp((static_cast<float>(y) + 0.5f) * source.height / height - 0.5f, 0.0f, static_cast<float>(source.height - 1));
			const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, source.height - 1);
			const float fy = sy - static_cast<float>(y0);
			for (int x = 0; x < width; x++) {
				const float sx = std::clamp((static_cast<float>(x) + 0.5f) * source.width / width - 0.5f, 0.0f, static_cast<float>(source.width - 1));
				const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, source.width - 1);
				const float fx = sx - static_cast<float>(x0);
				for (int c = 0; c < channels; c++) {
					auto at = [&](int px, int py) { return static_cast<float>(source.pixels[(static_cast<size_t>(py) * source.width + px) * channels + c]); };
					const float value = (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
					result.pixels[(static_cast<size_t>(y) * width + x) * channels + c] = static_cast<unsigned char>(value + 0.5f);
				}
			}
		}
		return result;
	}

	//64-bit FNV-1a
	void HashBytes(uint64_t& hash, const void* data, size_t size)
	{
		const unsigned char* bytes = static_cast<const unsigned char*>(data);
		for (size_t i = 0; i < size; i++) {
			hash = (hash ^ bytes[i]) * 1099511628211ull;
		}
	}

	//Identifies the files of a model and the targets of its levels: any change rebuilds its cache
	uint64_t CacheKey(const std::filesystem::path& folder, const std::vector<uint32_t>& targets)
	{
		uint64_t hash = 14695981039346656037ull;
		std::vector<std::filesystem::path> files;
		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
			const std::string extension = entry.path().extension().string();
			if (entry.is_regular_file() && (extension == ".gltf" || extension == ".bin")) {
				files.push_back(entry.path());
			}
		}
		std::sort(files.begin(), files.end());
		for (const std::filesystem::path& file : files) {
			const std::string name = file.filename().string();
			const uint64_t size = std::filesystem::file_size(file, error);
			const auto time = std::filesystem::last_write_time(file, error).time_since_epoch().count();
			HashBytes(hash, name.data(), name.size());
			HashBytes(hash, &size, sizeof(size));
			HashBytes(hash, &time, sizeof(time));
		}
		HashBytes(hash, targets.data(), targets.size() * sizeof(uint32_t));
		return hash;
	}

	GLuint CreateTexture(int width, int height, const unsigned char* rgba, bool mipmaps)
	{
		GLuint id = 0;
		glCreateTextures(GL_TEXTURE_2D, 1, &id);
		const int levels = mipmaps ? 1 + static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(width, height))))) : 1;
		glTextureStorage2D(id, levels, GL_RGBA8, width, height);
		glTextureSubImage2D(id, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_REPEAT);
		if (mipmaps) {
			glGenerateTextureMipmap(id);
			if (GLEW_ARB_texture_filter_anisotropic || GLEW_EXT_texture_filter_anisotropic) {
				GLfloat maxAnisotropy = 1.0f;
				glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
				glTextureParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maxAnisotropy));
			}
		}
		return id;
	}

	const char* StateName(VegetationModels::State state)
	{
		switch (state) {
		case VegetationModels::State::MISSING: return "not downloaded";
		case VegetationModels::State::LOADING: return "loading...";
		case VegetationModels::State::READY: return "ready";
		case VegetationModels::State::FAILED: return "failed";
		}
		return "";
	}
}

//Result of loading one model on a worker
struct VegetationModels::Loaded {
	State state = State::FAILED;
	std::string error;
	vegetation::ModelMesh mesh;
	bool fromCache = false;
	double ms = 0.0;
	//Packed maps per material (RGBA, width x height); empty = no texture, the fallback is used
	struct Maps {
		int width = 0, height = 0;
		std::vector<unsigned char> albedo, detail;
	};
	std::vector<Maps> maps;
};

//Offscreen view of one model for the "Vegetation" tab
struct VegetationModels::Preview {
	GLuint framebuffer = 0, color = 0, depth = 0, instances = 0;
	int selected = 0;
	int variant = 0;
	int lod = 0;
	float yaw = 0.6f;
	float pitch = 0.25f;
	bool autoRotate = true;
	bool wireframe = false;
	double lastTime = 0.0;

	~Preview()
	{
		glDeleteFramebuffers(1, &framebuffer);
		glDeleteTextures(1, &color);
		glDeleteRenderbuffers(1, &depth);
		glDeleteBuffers(1, &instances);
	}
};

VegetationModels::VegetationModels() = default;

VegetationModels::~VegetationModels()
{
	stopping = true;
	for (std::thread& worker : workers) {
		worker.join();
	}
	for (Model& model : models) {
		glDeleteVertexArrays(1, &model.vao);
		glDeleteBuffers(1, &model.vertexBuffer);
		glDeleteBuffers(1, &model.indexBuffer);
		glDeleteTextures(static_cast<GLsizei>(model.albedo.size()), model.albedo.data());
		glDeleteTextures(static_cast<GLsizei>(model.detail.size()), model.detail.data());
	}
	glDeleteTextures(1, &fallbackAlbedo);
	glDeleteTextures(1, &fallbackDetail);
}

bool VegetationModels::Initialize(const std::string& _resourceDir)
{
	resourceDir = _resourceDir;
	shader = std::make_unique<Shader>("res/shaders/VegetationShaders/vegetation_vertex.shader", "res/shaders/VegetationShaders/vegetation_fragment.shader");
	if (!shader->IsValid()) {
		std::cout << "[ERROR] VegetationModels: the shader could not be built" << std::endl;
		return false;
	}
	const std::filesystem::path manifestPath = std::filesystem::path(resourceDir) / "vegetation_models.json";
	try {
		std::ifstream file(manifestPath);
		if (!file) {
			std::cout << "[ERROR] VegetationModels: cannot open " << manifestPath.string() << std::endl;
			return false;
		}
		const nlohmann::json manifest = nlohmann::json::parse(file);
		const nlohmann::json& defaults = manifest.at("lodTriangles");
		for (const nlohmann::json& entry : manifest.at("models")) {
			Model model;
			model.name = entry.at("name").get<std::string>();
			model.asset = entry.at("asset").get<std::string>();
			model.kind = entry.value("kind", std::string("tree"));
			if (entry.contains("lods")) {
				model.lodTargets = entry.at("lods").get<std::vector<uint32_t>>();
			}
			else if (defaults.contains(model.kind)) {
				model.lodTargets = defaults.at(model.kind).get<std::vector<uint32_t>>();
			}
			if (model.lodTargets.empty() || model.lodTargets.size() > 8) {
				std::cout << "[ERROR] VegetationModels: " << model.name << " needs 1 to 8 levels of detail" << std::endl;
				return false;
			}
			models.push_back(model);
		}
	}
	catch (const std::exception& e) {
		std::cout << "[ERROR] VegetationModels: " << manifestPath.string() << ": " << e.what() << std::endl;
		return false;
	}

	const unsigned char neutralAlbedo[4] = { 128, 128, 128, 255 };
	const unsigned char neutralDetail[4] = { flatNormal, flatNormal, defaultRoughness, noOcclusion };
	fallbackAlbedo = CreateTexture(1, 1, neutralAlbedo, false);
	fallbackDetail = CreateTexture(1, 1, neutralDetail, false);

	glm::vec4 single[2] = { glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec4(0.0f) };
	preview = std::make_unique<Preview>();
	glCreateBuffers(1, &preview->instances);
	glNamedBufferStorage(preview->instances, sizeof(single), single, 0);
	glCreateTextures(GL_TEXTURE_2D, 1, &preview->color);
	glTextureStorage2D(preview->color, 1, GL_RGBA8, previewSize, previewSize);
	glTextureParameteri(preview->color, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(preview->color, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glCreateRenderbuffers(1, &preview->depth);
	glNamedRenderbufferStorage(preview->depth, GL_DEPTH_COMPONENT24, previewSize, previewSize);
	glCreateFramebuffers(1, &preview->framebuffer);
	glNamedFramebufferTexture(preview->framebuffer, GL_COLOR_ATTACHMENT0, preview->color, 0);
	glNamedFramebufferRenderbuffer(preview->framebuffer, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, preview->depth);

	const unsigned int workerCount = std::clamp(std::thread::hardware_concurrency() / 2, 1u, maxWorkers);
	for (unsigned int i = 0; i < workerCount; i++) {
		workers.emplace_back([this] {
			while (!stopping) {
				const size_t index = nextJob++;
				if (index >= models.size()) {
					return;
				}
				auto loaded = std::make_unique<Loaded>(Load(index));
				std::lock_guard lock(finishedMutex);
				finished.emplace_back(index, std::move(loaded));
			}
		});
	}
	std::cout << "[LOG] VegetationModels: loading " << models.size() << " models on " << workerCount << " threads" << std::endl;
	return true;
}

VegetationModels::Loaded VegetationModels::Load(size_t index) const
{
	const auto start = std::chrono::steady_clock::now();
	const Model& model = models[index];
	Loaded loaded;
	const std::filesystem::path folder = std::filesystem::path(resourceDir) / "models" / model.asset;
	const std::filesystem::path gltf = folder / (model.asset + ".gltf");
	if (!std::filesystem::exists(gltf)) {
		loaded.state = State::MISSING;
		loaded.error = "run python tools/fetch_models.py";
		return loaded;
	}
	const uint64_t key = CacheKey(folder, model.lodTargets);
	const std::filesystem::path cache = std::filesystem::path(resourceDir) / "cache" / (model.asset + ".mesh");
	if (vegetation::LoadCache(cache, loaded.mesh, key)) {
		loaded.fromCache = true;
	}
	else {
		std::vector<vegetation::Material> materials;
		std::vector<vegetation::SourceModel> variants;
		if (!vegetation::LoadGltf(gltf, materials, variants, loaded.error)) {
			loaded.state = State::FAILED;
			return loaded;
		}
		for (vegetation::SourceModel& variant : variants) {
			vegetation::MoveBaseToOrigin(variant);
		}
		loaded.mesh = vegetation::BuildLods(materials, variants, model.lodTargets);
		if (!vegetation::SaveCache(cache, loaded.mesh, key)) {
			std::cout << "[ERROR] VegetationModels: cannot write " << cache.string() << std::endl;
		}
	}

	for (const vegetation::Material& material : loaded.mesh.materials) {
		Loaded::Maps maps;
		Image albedo;
		if (!material.albedo.empty() && LoadImage(folder / material.albedo, 3, albedo)) {
			maps.width = albedo.width;
			maps.height = albedo.height;
			const size_t texels = static_cast<size_t>(maps.width) * maps.height;
			//The other maps are resized to the albedo if they differ
			auto load = [&](const std::string& name, int channels, Image& image) {
				if (name.empty() || !LoadImage(folder / name, channels, image)) {
					return false;
				}
				image = Resample(image, channels, maps.width, maps.height);
				return true;
			};
			Image alpha, normal, arm;
			const bool hasAlpha = load(material.alpha, 1, alpha);
			const bool hasNormal = load(material.normal, 3, normal);
			const bool hasArm = load(material.arm, 3, arm);
			maps.albedo.resize(texels * 4);
			maps.detail.resize(texels * 4);
			for (size_t i = 0; i < texels; i++) {
				maps.albedo[i * 4 + 0] = albedo.pixels[i * 3 + 0];
				maps.albedo[i * 4 + 1] = albedo.pixels[i * 3 + 1];
				maps.albedo[i * 4 + 2] = albedo.pixels[i * 3 + 2];
				maps.albedo[i * 4 + 3] = hasAlpha ? alpha.pixels[i] : 255;
				maps.detail[i * 4 + 0] = hasNormal ? normal.pixels[i * 3 + 0] : flatNormal;
				maps.detail[i * 4 + 1] = hasNormal ? normal.pixels[i * 3 + 1] : flatNormal;
				maps.detail[i * 4 + 2] = hasArm ? arm.pixels[i * 3 + 1] : defaultRoughness;
				maps.detail[i * 4 + 3] = hasArm ? arm.pixels[i * 3 + 0] : noOcclusion;
			}
		}
		loaded.maps.push_back(std::move(maps));
	}
	loaded.state = State::READY;
	loaded.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return loaded;
}

void VegetationModels::Install(size_t index, Loaded& loaded)
{
	Model& model = models[index];
	model.state = loaded.state;
	model.error = loaded.error;
	if (loaded.state != State::READY) {
		std::cout << "[LOG] VegetationModels: " << model.name << ": " << StateName(loaded.state) << " (" << loaded.error << ")" << std::endl;
		return;
	}
	const vegetation::ModelMesh& mesh = loaded.mesh;
	model.variants = mesh.variants;
	model.materials = mesh.materials;
	model.fromCache = loaded.fromCache;
	model.loadMs = loaded.ms;

	glCreateVertexArrays(1, &model.vao);
	glCreateBuffers(1, &model.vertexBuffer);
	glCreateBuffers(1, &model.indexBuffer);
	glNamedBufferStorage(model.vertexBuffer, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(vegetation::Vertex)), mesh.vertices.data(), 0);
	glNamedBufferStorage(model.indexBuffer, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)), mesh.indices.data(), 0);
	glVertexArrayVertexBuffer(model.vao, 0, model.vertexBuffer, 0, sizeof(vegetation::Vertex));
	glVertexArrayElementBuffer(model.vao, model.indexBuffer);
	const GLuint offsets[3] = { offsetof(vegetation::Vertex, position), offsetof(vegetation::Vertex, normal), offsetof(vegetation::Vertex, uv) };
	const GLint sizes[3] = { 3, 3, 2 };
	for (GLuint a = 0; a < 3; a++) {
		glEnableVertexArrayAttrib(model.vao, a);
		glVertexArrayAttribFormat(model.vao, a, sizes[a], GL_FLOAT, GL_FALSE, offsets[a]);
		glVertexArrayAttribBinding(model.vao, a, 0);
	}
	for (const Loaded::Maps& maps : loaded.maps) {
		if (maps.albedo.empty()) {
			model.albedo.push_back(0);
			model.detail.push_back(0);
			continue;
		}
		model.albedo.push_back(CreateTexture(maps.width, maps.height, maps.albedo.data(), true));
		model.detail.push_back(CreateTexture(maps.width, maps.height, maps.detail.data(), true));
	}
	std::cout << "[LOG] VegetationModels: " << model.name << " ready in " << static_cast<int>(model.loadMs) << " ms"
		<< (model.fromCache ? " (cache)" : " (levels built)") << std::endl;
}

void VegetationModels::Update()
{
	std::vector<std::pair<size_t, std::unique_ptr<Loaded>>> ready;
	{
		std::lock_guard lock(finishedMutex);
		ready.swap(finished);
	}
	for (auto& [index, loaded] : ready) {
		Install(index, *loaded);
	}
}

int VegetationModels::LoadingCount() const
{
	return static_cast<int>(std::count_if(models.begin(), models.end(), [](const Model& m) { return m.state == State::LOADING; }));
}

void VegetationModels::BeginDraw(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& viewPos, const glm::mat4& model,
	const LightSource& light, bool fogOn, float fogStart, float fogDistance)
{
	shader->Bind();
	shader->SetView(view);
	shader->SetProjection(projection);
	shader->SetViewPos(viewPos);
	shader->SetModel(model);
	light.SetLightUniforms(*shader);
	shader->SetUniform1i("fogOn", fogOn);
	shader->SetUniform1f("fogStart", fogStart);
	shader->SetUniform1f("fogDistance", fogDistance);
	shader->SetUniform1i("albedoMap", albedoUnit);
	shader->SetUniform1i("detailMap", detailUnit);
	shader->SetUniform1f("normalStrength", normalStrength);
	shader->SetUniform1f("specularStrength", specularStrength);
}

void VegetationModels::Draw(int modelIndex, int variant, int lod, int firstInstance, int instanceCount)
{
	const Model& model = models[modelIndex];
	if (model.state != State::READY || variant < 0 || variant >= static_cast<int>(model.variants.size())) {
		return;
	}
	const std::vector<vegetation::Lod>& lods = model.variants[variant].lods;
	if (lod < 0 || lod >= static_cast<int>(lods.size())) {
		return;
	}
	shader->SetUniform1i("firstInstance", firstInstance);
	glBindVertexArray(model.vao);
	for (const vegetation::Part& part : lods[lod].parts) {
		const vegetation::Material& material = model.materials[part.material];
		const GLuint albedo = model.albedo[part.material];
		glBindTextureUnit(albedoUnit, albedo ? albedo : fallbackAlbedo);
		glBindTextureUnit(detailUnit, albedo ? model.detail[part.material] : fallbackDetail);
		shader->SetUniform1i("alphaTest", material.alphaTest);
		shader->SetUniform1i("foliage", material.foliage);
		glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(part.indexCount), GL_UNSIGNED_INT,
			reinterpret_cast<const void*>(static_cast<uintptr_t>(part.firstIndex) * sizeof(uint32_t)), instanceCount);
	}
	glBindVertexArray(0);
}

void VegetationModels::DrawPreview(const LightSource& light)
{
	Preview& p = *preview;
	const double now = ImGui::GetTime();
	if (p.autoRotate) {
		p.yaw += static_cast<float>(now - p.lastTime) * 0.5f;
	}
	p.lastTime = now;
	RenderPreview(p.selected, p.variant, p.lod, p.yaw, p.pitch, p.wireframe, light);
}

std::vector<unsigned char> VegetationModels::PreviewImage(int modelIndex, int variant, int lod, float yaw, float pitch, bool wireframe, const LightSource& light)
{
	RenderPreview(modelIndex, variant, lod, yaw, pitch, wireframe, light);
	std::vector<unsigned char> pixels(static_cast<size_t>(previewSize) * previewSize * 4);
	glGetTextureImage(preview->color, 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(pixels.size()), pixels.data());
	return pixels;
}

int VegetationModels::PreviewSize()
{
	return previewSize;
}

void VegetationModels::RenderPreview(int modelIndex, int variantIndex, int lod, float yaw, float pitch, bool wireframe, const LightSource& light)
{
	Preview& p = *preview;
	const Model& model = models[modelIndex];
	const bool ready = model.state == State::READY && variantIndex >= 0 && variantIndex < static_cast<int>(model.variants.size());
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, p.framebuffer);
	glViewport(0, 0, previewSize, previewSize);
	glClearColor(0.16f, 0.18f, 0.22f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if (ready) {
		const vegetation::Variant& variant = model.variants[variantIndex];
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		const glm::vec3 center = (variant.boundsMin + variant.boundsMax) / 2.0f;
		const float radius = std::max(glm::length(variant.boundsMax - variant.boundsMin) / 2.0f, 0.01f);
		const glm::vec3 eye = center + radius * 2.6f * glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
		const glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::mat4 projection = glm::perspective(glm::radians(35.0f), 1.0f, radius * 0.1f, radius * 10.0f);
		BeginDraw(view, projection, eye, glm::mat4(1.0f), light, false, 0.0f, 1.0f);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, instanceBinding, p.instances);
		if (wireframe) {
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}
		Draw(modelIndex, variantIndex, std::clamp(lod, 0, static_cast<int>(variant.lods.size()) - 1), 0, 1);
		if (wireframe) {
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	if (scissor) {
		glEnable(GL_SCISSOR_TEST);
	}
}

void VegetationModels::ImGuiDraw(const LightSource& light)
{
	if (models.empty()) {
		ImGui::TextWrapped("The model list (res/vegetation/vegetation_models.json) could not be read.");
		return;
	}
	const int ready = static_cast<int>(std::count_if(models.begin(), models.end(), [](const Model& m) { return m.state == State::READY; }));
	const int missing = static_cast<int>(std::count_if(models.begin(), models.end(), [](const Model& m) { return m.state == State::MISSING; }));
	ImGui::Text("Models: %d of %d ready", ready, static_cast<int>(models.size()));
	if (LoadingCount() > 0) {
		ImGui::SameLine();
		ImGui::TextDisabled("(%d loading; the first load builds the levels of detail)", LoadingCount());
	}
	if (missing > 0) {
		ImGui::TextWrapped("%d models are not downloaded. Download them with: python tools/fetch_models.py", missing);
	}

	if (ImGui::BeginTable("models", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Model");
		ImGui::TableSetupColumn("Variants");
		ImGui::TableSetupColumn("Levels (triangles)");
		ImGui::TableSetupColumn("Load");
		ImGui::TableHeadersRow();
		for (int i = 0; i < static_cast<int>(models.size()); i++) {
			const Model& model = models[i];
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (ImGui::Selectable(model.name.c_str(), preview->selected == i, ImGuiSelectableFlags_SpanAllColumns)) {
				preview->selected = i;
				preview->variant = 0;
			}
			ImGui::TableNextColumn();
			if (model.state == State::READY) {
				ImGui::Text("%d", static_cast<int>(model.variants.size()));
				ImGui::TableNextColumn();
				//Levels of the first variant; the others are alike
				std::string levels;
				for (const vegetation::Lod& lod : model.variants[0].lods) {
					levels += (levels.empty() ? "" : " / ") + std::to_string(lod.triangles);
				}
				ImGui::TextUnformatted(levels.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%.0f ms%s", model.loadMs, model.fromCache ? "" : " *");
			}
			else {
				ImGui::TextDisabled("%s", StateName(model.state));
				if (!model.error.empty()) {
					ImGui::SetItemTooltip("%s", model.error.c_str());
				}
				ImGui::TableNextColumn();
				ImGui::TableNextColumn();
			}
		}
		ImGui::EndTable();
	}
	ImGui::TextDisabled("* levels built now and cached in res/vegetation/cache");

	Preview& p = *preview;
	const Model& model = models[p.selected];
	ImGui::SeparatorText(("Preview: " + model.name).c_str());
	if (model.state == State::READY) {
		const int variantCount = static_cast<int>(model.variants.size());
		p.variant = std::clamp(p.variant, 0, variantCount - 1);
		if (variantCount > 1) {
			ImGui::SliderInt("Variant", &p.variant, 0, variantCount - 1);
		}
		const vegetation::Variant& variant = model.variants[p.variant];
		const int lodCount = static_cast<int>(variant.lods.size());
		p.lod = std::clamp(p.lod, 0, lodCount - 1);
		for (int l = 0; l < lodCount; l++) {
			if (l > 0) {
				ImGui::SameLine();
			}
			ImGui::RadioButton(("LOD " + std::to_string(l)).c_str(), &p.lod, l);
		}
		ImGui::SameLine();
		ImGui::Text("%u of %zu triangles", variant.lods[p.lod].triangles, variant.sourceTriangles);
		const glm::vec3 size = variant.boundsMax - variant.boundsMin;
		ImGui::Text("Size %.2f x %.2f x %.2f m, %d materials", size.x, size.y, size.z, static_cast<int>(model.materials.size()));
		if (ImGui::IsItemHovered()) {
			std::string list;
			for (const vegetation::Material& m : model.materials) {
				list += m.name + (m.foliage ? " (foliage" : " (surface") + (m.alphaTest ? ", alpha)" : ")") + "\n";
			}
			ImGui::SetTooltip("%s", list.c_str());
		}
	}
	ImGui::Checkbox("Rotate", &p.autoRotate);
	ImGui::SameLine();
	ImGui::Checkbox("Wireframe", &p.wireframe);
	ImGui::SliderFloat("Normal strength", &normalStrength, 0.0f, 2.0f);
	ImGui::SliderFloat("Specular", &specularStrength, 0.0f, 1.0f);

	DrawPreview(light);
	const float side = std::min(ImGui::GetContentRegionAvail().x, static_cast<float>(previewSize));
	//An invisible button under the image, so dragging it turns the model
	const ImVec2 corner = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("preview", ImVec2(side, side));
	ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(static_cast<uintptr_t>(p.color)), corner,
		ImVec2(corner.x + side, corner.y + side), ImVec2(0, 1), ImVec2(1, 0));
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		const ImVec2 delta = ImGui::GetIO().MouseDelta;
		p.yaw -= delta.x * 0.01f;
		p.pitch = std::clamp(p.pitch + delta.y * 0.01f, -1.2f, 1.4f);
		p.autoRotate = false;
	}
}
