#include "VegetationCheck.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include <lodepng.h>

#include "LightSource.h"
#include "VegetationModels.h"

namespace {
	//Waits until no model is loading any more, installing them as they finish
	void WaitForModels(VegetationModels& models)
	{
		while (models.LoadingCount() > 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			models.Update();
		}
	}

	//All levels of one model in a grid: a row per variant, shaded, and the first variant as wireframe in the last row.
	//The camera looks from the side of the sun
	bool SaveSheet(VegetationModels& models, int index, const LightSource& light, const std::filesystem::path& file)
	{
		const VegetationModels::Model& model = models.GetModels()[index];
		const int size = VegetationModels::PreviewSize();
		const int columns = static_cast<int>(model.variants[0].lods.size());
		const int variants = static_cast<int>(model.variants.size());
		const int rows = variants + 1;
		const int width = size * columns, height = size * rows;
		const glm::vec3 sun = light.GetDirection();
		const float yaw = std::atan2(sun.x, sun.z) + 0.5f;
		std::vector<unsigned char> sheet(static_cast<size_t>(width) * height * 4);
		for (int row = 0; row < rows; row++) {
			const bool wireframe = row == variants;
			const int variant = wireframe ? 0 : row;
			for (int lod = 0; lod < columns; lod++) {
				const std::vector<unsigned char> image = models.PreviewImage(index, variant, lod, yaw, 0.2f, wireframe, light);
				for (int y = 0; y < size; y++) {
					//The image is bottom-up, PNG top-down
					const unsigned char* source = &image[static_cast<size_t>(size - 1 - y) * size * 4];
					unsigned char* target = &sheet[(static_cast<size_t>(row * size + y) * width + static_cast<size_t>(lod) * size) * 4];
					std::copy(source, source + static_cast<size_t>(size) * 4, target);
				}
			}
		}
		return lodepng::encode(file.string(), sheet, static_cast<unsigned>(width), static_cast<unsigned>(height)) == 0;
	}

	//Impostor atlases of all variants of a model side by side, on the background of the previews
	bool SaveImpostors(const VegetationModels& models, int index, const std::filesystem::path& file)
	{
		const int size = VegetationModels::ImpostorSize();
		const int variants = static_cast<int>(models.GetModels()[index].variants.size());
		const int width = size * variants;
		std::vector<unsigned char> sheet(static_cast<size_t>(width) * size * 4);
		const float background[3] = { 0.16f * 255.0f, 0.18f * 255.0f, 0.22f * 255.0f };
		for (int v = 0; v < variants; v++) {
			const std::vector<unsigned char> atlas = models.ImpostorAtlas(index, v);
			for (int y = 0; y < size; y++) {
				for (int x = 0; x < size; x++) {
					//Bottom-up and premultiplied: over the background, top-down
					const unsigned char* source = &atlas[(static_cast<size_t>(size - 1 - y) * size + x) * 4];
					unsigned char* target = &sheet[(static_cast<size_t>(y) * width + static_cast<size_t>(v) * size + x) * 4];
					const float uncovered = 1.0f - source[3] / 255.0f;
					for (int c = 0; c < 3; c++) {
						target[c] = static_cast<unsigned char>(std::min(255.0f, source[c] + background[c] * uncovered));
					}
					target[3] = 255;
				}
			}
		}
		return lodepng::encode(file.string(), sheet, static_cast<unsigned>(width), static_cast<unsigned>(size)) == 0;
	}
}

int RunVegetationPreview(const std::string& folder)
{
	if (!glfwInit()) {
		std::cout << "[ERROR] Failed to initialize GLFW" << std::endl;
		return -1;
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	GLFWwindow* window = glfwCreateWindow(64, 64, "Vegetation preview", nullptr, nullptr);
	if (!window) {
		std::cout << "[ERROR] OpenGL 4.5 is not available" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	int result = -1;
	if (glewInit() != GLEW_OK) {
		std::cout << "[ERROR] Failed to initialize GLEW" << std::endl;
	}
	else {
		//The GL objects must go before the context does
		VegetationModels models;
		if (models.Initialize("res/vegetation")) {
			WaitForModels(models);
			std::error_code error;
			std::filesystem::create_directories(folder, error);
			LightSource light;
			result = 0;
			std::printf("\n%-16s %8s %10s  %-24s %8s\n", "model", "variant", "source", "levels (triangles)", "load ms");
			for (int i = 0; i < static_cast<int>(models.GetModels().size()); i++) {
				const VegetationModels::Model& model = models.GetModels()[i];
				if (model.state != VegetationModels::State::READY) {
					std::printf("%-16s %s\n", model.name.c_str(), model.error.c_str());
					if (model.state == VegetationModels::State::FAILED) {
						result = 1;
					}
					continue;
				}
				for (size_t v = 0; v < model.variants.size(); v++) {
					const vegetation::Variant& variant = model.variants[v];
					std::string levels;
					for (const vegetation::Lod& lod : variant.lods) {
						levels += (levels.empty() ? "" : " / ") + std::to_string(lod.triangles);
					}
					const glm::vec3 size = variant.boundsMax - variant.boundsMin;
					std::printf("%-16s %8zu %10zu  %-24s %8.0f%s  %.2f x %.2f x %.2f m\n", v == 0 ? model.name.c_str() : "", v, variant.sourceTriangles,
						levels.c_str(), model.loadMs, model.fromCache ? "" : "*", size.x, size.y, size.z);
				}
				const std::filesystem::path file = std::filesystem::path(folder) / (model.asset + ".png");
				if (!SaveSheet(models, i, light, file)) {
					std::cout << "[ERROR] cannot write " << file.string() << std::endl;
					result = 1;
				}
				const std::filesystem::path impostors = std::filesystem::path(folder) / (model.asset + "_impostors.png");
				if (models.HasImpostors(i) && !SaveImpostors(models, i, impostors)) {
					std::cout << "[ERROR] cannot write " << impostors.string() << std::endl;
					result = 1;
				}
			}
			std::cout << "\n* levels built now. Pictures in " << std::filesystem::absolute(folder).string() << std::endl;
		}
	}
	glfwDestroyWindow(window);
	glfwTerminate();
	return result;
}
