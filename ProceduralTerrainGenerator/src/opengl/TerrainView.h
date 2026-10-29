#pragma once

#include <memory>
#include <vector>

#include "glm/glm.hpp"

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
	bool map2d = false;
};

//Renders a height field as a tessellated patch grid. Owns the mesh, the shader and the textures,
//so generation subsystems only hand it finished maps.
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

	//Size of the patch grid in world units (before modelScale)
	float GetWorldWidth() const { return width * worldUnitsPerTexel; }
	float GetWorldHeight() const { return height * worldUnitsPerTexel; }

	void SetLayer(Layer layer, const HeightField& map);
	void SetBiomeColors(const std::vector<glm::vec3>& colors, int _width, int _height);
	bool HasLayer(Layer layer) const { return textures[static_cast<int>(layer)] != nullptr; }

	//Draws the patch grid displaced by the selected height layer
	//@param heightLayer - TERRAIN or OVERLAY, the layer used as the height map
	//@param offset - translation applied after scaling (e.g. to draw a second map next to the first)
	void Draw(Renderer& renderer, Camera& camera, LightSource& light, const TerrainRenderSettings& settings,
		Layer heightLayer = Layer::TERRAIN, glm::vec3 offset = glm::vec3(0.0f));

private:
	void BuildMesh();

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
};
