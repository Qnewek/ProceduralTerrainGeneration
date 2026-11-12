#include "TerrainMaterials.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

#include <nlohmann/json.hpp>
#include "imgui.h"
#include "stb_image.h"

#include "Biome.h"

//The splat map holds a weight for every material of the list
static_assert(TerrainMaterials::maxMaterials == biome::maxMaterials);

namespace {
	//Values of a map the material does not have
	constexpr unsigned char flatHeight = 128;
	constexpr unsigned char flatNormal = 128;
	constexpr unsigned char defaultRoughness = 200;
	constexpr unsigned char noOcclusion = 255;

	//One material packed into the layouts of both arrays (size * size * 4 bytes each)
	struct PackedMaterial {
		int size = 0;
		std::vector<unsigned char> albedo, detail;
		std::string error;		//empty if the material loaded
	};

	struct Image {
		int width = 0, height = 0;
		std::vector<unsigned char> pixels;
	};

	//Loads an image with the given number of channels, rows from the bottom up as OpenGL expects them
	//(so V grows up the image, as the OpenGL normal map convention assumes)
	//@return false if the file is missing or cannot be decoded
	bool LoadImage(const std::filesystem::path& path, int channels, Image& image)
	{
		if (!std::filesystem::exists(path)) {
			return false;
		}
		stbi_set_flip_vertically_on_load_thread(1);
		unsigned char* data = stbi_load(path.string().c_str(), &image.width, &image.height, nullptr, channels);
		if (!data) {
			std::cout << "[ERROR] TerrainMaterials: cannot decode " << path.string() << ": " << stbi_failure_reason() << std::endl;
			return false;
		}
		image.pixels.assign(data, data + static_cast<size_t>(image.width) * image.height * channels);
		stbi_image_free(data);
		return true;
	}

	//Loads the maps of one material and packs them; albedo is required, the other maps fall back to neutral values
	//Runs on a worker thread: no OpenGL here
	PackedMaterial LoadMaterial(const std::filesystem::path& folder)
	{
		PackedMaterial packed;
		Image albedo;
		if (!LoadImage(folder / "albedo.jpg", 3, albedo)) {
			packed.error = "no " + (folder / "albedo.jpg").string();
			return packed;
		}
		if (albedo.width != albedo.height) {
			packed.error = "albedo is not square";
			return packed;
		}
		packed.size = albedo.width;
		const size_t texels = static_cast<size_t>(packed.size) * packed.size;

		//A map of another size than the albedo is ignored, like a missing one
		auto load = [&](const char* name, int channels, Image& image) {
			if (!LoadImage(folder / name, channels, image)) {
				return false;
			}
			if (image.width != packed.size || image.height != packed.size) {
				std::cout << "[ERROR] TerrainMaterials: " << (folder / name).string() << " has another size than the albedo, ignored" << std::endl;
				return false;
			}
			return true;
		};
		Image height, normal, roughness, ao;
		const bool hasHeight = load("height.jpg", 1, height);
		const bool hasNormal = load("normal.jpg", 3, normal);
		const bool hasRoughness = load("roughness.jpg", 1, roughness);
		const bool hasAo = load("ao.jpg", 1, ao);

		packed.albedo.resize(texels * 4);
		packed.detail.resize(texels * 4);
		for (size_t i = 0; i < texels; i++) {
			packed.albedo[i * 4 + 0] = albedo.pixels[i * 3 + 0];
			packed.albedo[i * 4 + 1] = albedo.pixels[i * 3 + 1];
			packed.albedo[i * 4 + 2] = albedo.pixels[i * 3 + 2];
			packed.albedo[i * 4 + 3] = hasHeight ? height.pixels[i] : flatHeight;
			packed.detail[i * 4 + 0] = hasNormal ? normal.pixels[i * 3 + 0] : flatNormal;
			packed.detail[i * 4 + 1] = hasNormal ? normal.pixels[i * 3 + 1] : flatNormal;
			packed.detail[i * 4 + 2] = hasRoughness ? roughness.pixels[i] : defaultRoughness;
			packed.detail[i * 4 + 3] = hasAo ? ao.pixels[i] : noOcclusion;
		}
		return packed;
	}

	//Layer of a material without textures: its fallback color, flat and smooth
	PackedMaterial FallbackMaterial(int size, glm::vec3 color)
	{
		PackedMaterial packed;
		packed.size = size;
		const size_t texels = static_cast<size_t>(size) * size;
		packed.albedo.resize(texels * 4);
		packed.detail.resize(texels * 4);
		const glm::vec3 rgb = glm::clamp(color, 0.0f, 1.0f) * 255.0f + 0.5f;
		for (size_t i = 0; i < texels; i++) {
			packed.albedo[i * 4 + 0] = static_cast<unsigned char>(rgb.r);
			packed.albedo[i * 4 + 1] = static_cast<unsigned char>(rgb.g);
			packed.albedo[i * 4 + 2] = static_cast<unsigned char>(rgb.b);
			packed.albedo[i * 4 + 3] = flatHeight;
			packed.detail[i * 4 + 0] = flatNormal;
			packed.detail[i * 4 + 1] = flatNormal;
			packed.detail[i * 4 + 2] = defaultRoughness;
			packed.detail[i * 4 + 3] = noOcclusion;
		}
		return packed;
	}

	//Texture array with a full mip chain, repeated and filtered anisotropically
	GLuint CreateArray(int size, int layers)
	{
		GLuint id = 0;
		glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &id);
		const int levels = 1 + static_cast<int>(std::floor(std::log2(static_cast<float>(size))));
		glTextureStorage3D(id, levels, GL_RGBA8, size, size, layers);
		glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_REPEAT);
		if (GLEW_ARB_texture_filter_anisotropic || GLEW_EXT_texture_filter_anisotropic) {
			GLfloat maxAnisotropy = 1.0f;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
			glTextureParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(16.0f, maxAnisotropy));
		}
		return id;
	}
}

TerrainMaterials::~TerrainMaterials()
{
	glDeleteTextures(1, &albedoArray);
	glDeleteTextures(1, &detailArray);
	glDeleteBuffers(1, &blockBuffer);
}

bool TerrainMaterials::Initialize(const std::string& manifestPath, const std::string& textureDir)
{
	//Cliff materials by name, resolved once the whole list is read
	std::vector<std::string> cliffNames;
	try {
		std::ifstream file(manifestPath);
		if (!file) {
			std::cout << "[ERROR] TerrainMaterials: cannot open " << manifestPath << std::endl;
			return false;
		}
		const nlohmann::json manifest = nlohmann::json::parse(file);
		for (const nlohmann::json& entry : manifest.at("materials")) {
			cliffNames.push_back(entry.value("cliffMaterial", std::string()));
			TerrainMaterial material;
			material.name = entry.at("name").get<std::string>();
			material.asset = entry.at("asset").get<std::string>();
			material.tileSize = entry.value("tileSize", material.tileSize);
			if (entry.contains("fallbackColor")) {
				const std::vector<float> color = entry.at("fallbackColor").get<std::vector<float>>();
				if (color.size() == 3) {
					material.fallbackColor = glm::vec3(color[0], color[1], color[2]);
				}
			}
			if (material.tileSize <= 0.0f) {
				std::cout << "[ERROR] TerrainMaterials: material " << material.name << " has tileSize <= 0" << std::endl;
				return false;
			}
			materials.push_back(material);
		}
	}
	catch (const std::exception& e) {
		std::cout << "[ERROR] TerrainMaterials: " << manifestPath << ": " << e.what() << std::endl;
		return false;
	}
	if (materials.empty() || static_cast<int>(materials.size()) > maxMaterials) {
		std::cout << "[ERROR] TerrainMaterials: the list needs 1 to " << maxMaterials << " materials, has " << materials.size() << std::endl;
		materials.clear();
		return false;
	}
	for (size_t i = 0; i < materials.size(); i++) {
		materials[i].cliffMaterial = static_cast<int>(i);
		if (cliffNames[i].empty()) {
			continue;
		}
		const auto found = std::find_if(materials.begin(), materials.end(), [&](const TerrainMaterial& m) { return m.name == cliffNames[i]; });
		if (found == materials.end()) {
			std::cout << "[ERROR] TerrainMaterials: " << materials[i].name << " has an unknown cliff material \"" << cliffNames[i] << "\", ignored" << std::endl;
			continue;
		}
		materials[i].cliffMaterial = static_cast<int>(found - materials.begin());
	}

	//JPEG decoding takes most of the time, so every material decodes on its own thread
	std::vector<std::future<PackedMaterial>> jobs;
	for (const TerrainMaterial& material : materials) {
		const std::filesystem::path folder = std::filesystem::path(textureDir) / material.asset;
		jobs.push_back(std::async(std::launch::async, LoadMaterial, folder));
	}
	std::vector<PackedMaterial> packed;
	for (std::future<PackedMaterial>& job : jobs) {
		packed.push_back(job.get());
	}

	//All layers of an array have one size: the size of the first material that loaded
	layerSize = 0;
	for (const PackedMaterial& p : packed) {
		if (p.error.empty()) {
			layerSize = p.size;
			break;
		}
	}
	int loadedCount = 0;
	for (size_t i = 0; i < materials.size(); i++) {
		if (packed[i].error.empty() && packed[i].size != layerSize) {
			packed[i].error = "size " + std::to_string(packed[i].size) + " differs from " + std::to_string(layerSize);
		}
		materials[i].loaded = packed[i].error.empty();
		if (materials[i].loaded) {
			loadedCount++;
		}
		else {
			std::cout << "[LOG] TerrainMaterials: " << materials[i].name << " uses its fallback color (" << packed[i].error << ")" << std::endl;
		}
	}
	if (layerSize == 0) {
		layerSize = 4;	//no textures at all: tiny layers of fallback colors
		std::cout << "[LOG] TerrainMaterials: no terrain textures, run tools/fetch_textures.py to download them" << std::endl;
	}

	const int layers = static_cast<int>(materials.size());
	albedoArray = CreateArray(layerSize, layers);
	detailArray = CreateArray(layerSize, layers);
	for (int i = 0; i < layers; i++) {
		if (!materials[i].loaded) {
			packed[i] = FallbackMaterial(layerSize, materials[i].fallbackColor);
		}
		const PackedMaterial& p = packed[i];
		glTextureSubImage3D(albedoArray, 0, 0, 0, i, layerSize, layerSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.albedo.data());
		glTextureSubImage3D(detailArray, 0, 0, 0, i, layerSize, layerSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.detail.data());
	}
	glGenerateTextureMipmap(albedoArray);
	glGenerateTextureMipmap(detailArray);

	glCreateBuffers(1, &blockBuffer);
	glNamedBufferStorage(blockBuffer, sizeof(Block), nullptr, GL_DYNAMIC_STORAGE_BIT);
	blockOutdated = true;

	std::cout << "[LOG] TerrainMaterials: " << loadedCount << " of " << layers << " materials loaded, " << layerSize << " px" << std::endl;
	return true;
}

void TerrainMaterials::UploadBlock()
{
	Block block{};
	for (size_t i = 0; i < materials.size(); i++) {
		block.params[i] = glm::vec4(1.0f / materials[i].tileSize, static_cast<float>(materials[i].cliffMaterial), 0.0f, 0.0f);
	}
	block.materialCount = static_cast<int>(materials.size());
	block.previewMaterial = previewMaterial;
	block.textureScale = textureScale;
	block.normalStrength = normalStrength;
	block.cliffSlopeStart = glm::radians(cliffSlopeStart);
	block.cliffSlopeEnd = glm::radians(cliffSlopeEnd);
	block.heightInfluence = heightInfluence;
	block.blendDepth = heightBlending ? blendDepth : 0.0f;
	block.triplanarStart = glm::radians(triplanarStart);
	block.triplanarEnd = glm::radians(triplanarEnd);
	block.specularStrength = specularStrength;
	//Off = no second scale, no macro variation and averages so far away they never show
	block.farUvScale = farUvScale;
	block.farUvMix = antiTiling ? farUvMix : 0.0f;
	block.farUvStart = farUvStart;
	block.farUvEnd = farUvEnd;
	block.averageStart = antiTiling ? averageStart : 1.0e30f;
	block.averageEnd = antiTiling ? averageEnd : 2.0e30f;
	block.macroStrength = antiTiling ? macroStrength : 0.0f;
	block.macroSize = macroSize;
	glNamedBufferSubData(blockBuffer, 0, sizeof(Block), &block);
	blockOutdated = false;
}

void TerrainMaterials::Bind()
{
	if (!blockBuffer) {
		return;
	}
	if (blockOutdated) {
		UploadBlock();
	}
	glBindTextureUnit(albedoUnit, albedoArray);
	glBindTextureUnit(detailUnit, detailArray);
	glBindBufferBase(GL_UNIFORM_BUFFER, blockBinding, blockBuffer);
}

void TerrainMaterials::ImGuiDraw()
{
	if (materials.empty()) {
		ImGui::TextWrapped("The material list (res/textures/terrain_materials.json) could not be read.");
		return;
	}
	const int loadedCount = static_cast<int>(std::count_if(materials.begin(), materials.end(), [](const TerrainMaterial& m) { return m.loaded; }));
	ImGui::Text("Textures: %d of %d materials, %d px", loadedCount, static_cast<int>(materials.size()), layerSize);
	if (loadedCount < static_cast<int>(materials.size())) {
		ImGui::TextWrapped("Missing textures show as flat colors. Download them with: python tools/fetch_textures.py");
	}
	ImGui::TextWrapped("Shown in the TEXTURED display mode. With biomes each biome chooses its materials (biome table); "
		"without them the whole terrain has the material below.");

	if (ImGui::BeginCombo("Material", materials[previewMaterial].name.c_str())) {
		for (int i = 0; i < static_cast<int>(materials.size()); i++) {
			const bool selected = i == previewMaterial;
			std::string label = materials[i].name + (materials[i].loaded ? "" : " (no textures)");
			if (ImGui::Selectable(label.c_str(), selected)) {
				previewMaterial = i;
				blockOutdated = true;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::SliderFloat("Texture scale", &textureScale, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
		textureScale = std::max(textureScale, 0.01f);
		blockOutdated = true;
	}
	if (ImGui::SliderFloat("Normal strength", &normalStrength, 0.0f, 2.0f)) {
		blockOutdated = true;
	}

	ImGui::SeparatorText("Cliffs");
	if (ImGui::DragFloatRange2("Cliff slope", &cliffSlopeStart, &cliffSlopeEnd, 0.2f, 0.0f, 90.0f, "%.1f deg", nullptr, ImGuiSliderFlags_AlwaysClamp)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Slopes over which each material gives way to its cliff material (terrain_materials.json):\n"
		"none below the first angle, all of it above the second");

	ImGui::SeparatorText("Blending");
	if (ImGui::Checkbox("Height-based blending", &heightBlending)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Where materials meet, the higher parts of each texture (stones, clumps) show through the other one,\n"
		"instead of a plain crossfade");
	ImGui::BeginDisabled(!heightBlending);
	if (ImGui::SliderFloat("Height influence", &heightInfluence, 0.0f, 1.0f)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("How much the texture height counts against the material weight; 0 = only the weights");
	if (ImGui::SliderFloat("Blend depth", &blendDepth, 0.01f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Width of the transition: small = sharp edges around stones, large = soft");
	ImGui::EndDisabled();

	ImGui::SeparatorText("Projection and lighting");
	if (ImGui::DragFloatRange2("Triplanar slope", &triplanarStart, &triplanarEnd, 0.2f, 0.0f, 90.0f, "%.1f deg", nullptr, ImGuiSliderFlags_AlwaysClamp)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Slopes over which the textures are also projected from the sides, so steep rock is not stretched.\n"
		"Below the first angle only the projection from above (cheapest)");
	if (ImGui::SliderFloat("Specular", &specularStrength, 0.0f, 2.0f)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Strength of the sun highlights; smooth materials (low roughness) shine more and sharper");

	ImGui::SeparatorText("Anti-tiling");
	if (ImGui::Checkbox("Anti-tiling", &antiTiling)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Hides the repeats of the textures: a second texture scale with distance,\n"
		"average colors far away and large-scale brightness/hue variation");
	ImGui::BeginDisabled(!antiTiling);
	if (ImGui::SliderFloat("Far scale", &farUvScale, 0.05f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Size of the second texture scale relative to the first (0.27 = 3.7 times larger repeats)");
	if (ImGui::SliderFloat("Far mix", &farUvMix, 0.0f, 1.0f)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Share of the second scale far away; costs a second texture read beyond the first distance");
	if (ImGui::DragFloatRange2("Far distance", &farUvStart, &farUvEnd, 1.0f, 0.0f, 5000.0f, "%.0f", nullptr, ImGuiSliderFlags_AlwaysClamp)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Distance (world units) over which the second scale is mixed in");
	if (ImGui::DragFloatRange2("Average distance", &averageStart, &averageEnd, 5.0f, 0.0f, 20000.0f, "%.0f", nullptr, ImGuiSliderFlags_AlwaysClamp)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Distance over which the materials fade into their average colors; beyond it the textures are not read");
	if (ImGui::SliderFloat("Macro variation", &macroStrength, 0.0f, 0.5f)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Brightness and hue variation over large areas; 0 = off");
	if (ImGui::SliderFloat("Macro size", &macroSize, 10.0f, 2000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
		blockOutdated = true;
	}
	ImGui::SetItemTooltip("Wavelength of the macro variation in world units");
	ImGui::EndDisabled();
}
