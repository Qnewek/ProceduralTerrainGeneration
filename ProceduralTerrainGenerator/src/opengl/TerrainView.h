#pragma once

#include <memory>
#include <vector>

#include "glm/glm.hpp"

#include "GpuSurfacePass.h"
#include "Renderer.h"
#include "VertexBufferLayout.h"
#include "Camera.h"
#include "LightSource.h"
#include "Grid.h"
#include "utilities.h"

//Display parameters of a terrain view, edited by the UI
struct TerrainRenderSettings {
	float modelScale = 1.0f;
	float heightScale = 1.0f;
	float topoStep = 10.0f;
	float topoBandWidth = 0.2f;		//0 disables isolines
	utilities::heightMapMode displayMode = utilities::heightMapMode::GREYSCALE;
	bool wireFrame = false;
	bool map2d = false;		//top-down orthographic view instead of the camera
};

//Renders a height field as a tessellated patch grid. Owns the mesh, the shader and the textures,
//so generation subsystems only hand it finished maps. Every height layer has a surface map (gradient and Laplacian,
//surface::Derivatives) for normals, slope and curvature; Draw recomputes it on the GPU after the layer changes.
class TerrainView
{
public:
	//Texture slots; the value is the texture unit the layer is bound to
	enum class Layer {
		TERRAIN = 0,	//main height map
		OVERLAY = 1,	//alternative height map (noise preview, eroded map)
		BIOMES = 2		//biome colors (RGB)
	};

	TerrainView();
	~TerrainView();

	bool Initialize(int _width, int _height);
	//Rebuilds the mesh if the map size changed
	//@return true if the mesh was rebuilt
	bool Resize(int _width, int _height);
	//Rebuilds the terrain and surface shaders from their source files (hot-reload)
	//@return true if the terrain shader was replaced
	bool ReloadShader();

	int GetMapWidth() const { return width; }
	int GetMapHeight() const { return height; }

	//Size of the patch grid in world units (before modelScale)
	float GetWorldWidth() const { return width * worldUnitsPerTexel; }
	float GetWorldHeight() const { return height * worldUnitsPerTexel; }

	//Area (XZ, world units before modelScale) framed by the 2D map view; defaults to the map itself.
	//Set it when drawing several maps side by side so all of them fit.
	void SetFlatViewBounds(glm::vec2 min, glm::vec2 max) { flatMin = min; flatMax = max; }
	void ResetFlatViewBounds();

	//Layer textures are allocated once per map size; same-size updates only upload the new data
	void SetLayer(Layer layer, const HeightField& map);
	//Texture of a layer with the given size, for writing to it directly (GL_RGBA32F for BIOMES, GL_R32F otherwise).
	//Marks the surface map of a height layer as out of date, so write the new contents before the next Draw
	TextureClass& LayerTexture(Layer layer, int _width, int _height);
	//Copies a height layer to the CPU, waiting for the GPU; the layer must exist
	HeightField ReadLayer(Layer layer) const;
	bool HasLayer(Layer layer) const { return textures[static_cast<int>(layer)] != nullptr; }

	//Draws the patch grid displaced by the selected height layer
	//@param heightLayer - TERRAIN or OVERLAY, the layer used as the height map
	//@param offset - translation applied after scaling (e.g. to draw a second map next to the first)
	void Draw(Renderer& renderer, Camera& camera, LightSource& light, const TerrainRenderSettings& settings,
		Layer heightLayer = Layer::TERRAIN, glm::vec3 offset = glm::vec3(0.0f));

private:
	void BuildMesh();
	//Sets view/projection of a top-down orthographic camera framing the flat view bounds
	void SetFlatCameraUniforms(const TerrainRenderSettings& settings);
	//Recomputes the surface map of a height layer if the layer changed since the last time
	void UpdateSurface(Layer heightLayer);

	//Texture unit of the surface map of the drawn height layer
	static constexpr int surfaceUnit = 3;

	glm::vec2 flatMin = glm::vec2(0.0f), flatMax = glm::vec2(0.0f);

	//One map texel spans this many world units
	static constexpr float worldUnitsPerTexel = 4.0f;

	int width = 0, height = 0;
	int mapResolution = 50;
	unsigned int stride = 5;
	std::vector<float> vertices;

	VertexBufferLayout layout;
	std::unique_ptr<VertexArray> vao;
	std::unique_ptr<VertexBuffer> vertexBuffer;
	std::unique_ptr<Shader> shader;
	std::unique_ptr<TextureClass> textures[3];

	GpuSurfacePass surfacePass;
	//Surface maps of TERRAIN and OVERLAY (GL_RGBA32F), indexed like the layers
	std::unique_ptr<TextureClass> surfaces[2];
	bool surfaceOutdated[2] = { true, true };
};
