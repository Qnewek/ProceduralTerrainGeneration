#pragma once

#include <memory>

#include "GpuHeightGenerator.h"
#include "Noise.h"
#include "Shader.h"
#include "TextureClass.h"

//Small 2D picture of one noise over the whole finite map, for ImGui::Image: the noise goes to a value texture
//(at most maxSize texels along the longer side) and preview_compute.shader turns it into gray levels,
//black = -1, white = 1. Only for the thread owning the GL context.
class NoisePreview
{
public:
	static constexpr int maxSize = 512;
	//Largest height of the picture in the UI, in pixels
	static constexpr float maxDrawHeight = 220.0f;

	//@return false if the shader could not be built
	bool Initialize();
	void ReloadShader() { shader->Reload(); }

	//Generates the picture of the noise as it lies on a map of the given size (islands fill that map)
	void Generate(GpuHeightGenerator& gpu, const noise::NoiseConfigParameters& config, int mapWidth, int mapHeight);

	//Draws the picture with ImGui, as wide as the available space, row y = 0 at the top like the 2D map; nothing before Generate
	void ImGuiDraw() const;

private:
	std::unique_ptr<Shader> shader;
	std::unique_ptr<TextureClass> values, colors;
};
