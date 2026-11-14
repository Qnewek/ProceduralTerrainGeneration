#pragma once

#include <memory>
#include <vector>

#include "glm/glm.hpp"

#include "FrameUniforms.h"
#include "GpuSurfacePass.h"
#include "Renderer.h"
#include "VertexBufferLayout.h"
#include "Camera.h"
#include "LightSource.h"
#include "Grid.h"
#include "utilities.h"

class ShadowMap;

//Display parameters of a terrain view, edited by the UI
struct TerrainRenderSettings {
	float modelScale = 1.0f;
	float heightScale = 1.0f;
	float topoStep = 10.0f;
	float topoBandWidth = 0.2f;		//0 disables isolines
	utilities::heightMapMode displayMode = utilities::heightMapMode::GREYSCALE;
	bool wireFrame = false;
	bool map2d = false;		//top-down orthographic view instead of the camera
	float tessPixels = 6.0f;	//3D view: length of a terrain triangle edge on screen in pixels (smaller = more detail); the 2D map ignores it
	//Sun shadows of this frame (set by the subsystem after rendering them), added to the FrameBlock of the 3D view; nullptr = none
	const ShadowMap* shadows = nullptr;
};

//Renders a height field as a tessellated patch grid. Owns the mesh, the shader and the textures,
//so generation subsystems only hand it finished maps. Every height layer has a surface map (gradient and Laplacian,
//GpuSurfacePass) for normals, slope and curvature; Draw recomputes it on the GPU after the layer changes.
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

	//If the camera is not above the map, moves it to a raised point beyond the +Z edge looking at the map center,
	//no farther than allows the view distance (e.g. after leaving the infinite world far from the map)
	void BringCameraToMap(Camera& camera, const TerrainRenderSettings& settings) const;

	//Layer textures are allocated once per map size; same-size updates only upload the new data
	void SetLayer(Layer layer, const HeightField& map);
	//Texture of a layer with the given size, for writing to it directly (GL_RGBA32F for BIOMES, GL_R32F otherwise).
	//Marks the surface map of a height layer as out of date, so write the new contents before the next Draw
	TextureClass& LayerTexture(Layer layer, int _width, int _height);
	//Copies a height layer to the CPU, waiting for the GPU; the layer must exist
	HeightField ReadLayer(Layer layer) const;
	bool HasLayer(Layer layer) const { return textures[static_cast<int>(layer)] != nullptr; }
	//Splat map (share of every surface material, GpuHeightGenerator::GenerateBiomes) with the given size, for writing to it directly.
	//Without one the TEXTURED mode draws the material chosen in TerrainMaterials
	TextureClass& SplatTexture(int _width, int _height);
	//Drops the splat map, e.g. when biomes are turned off
	void ReleaseSplat() { splat.reset(); }

	//Draws the patch grid displaced by the selected height layer
	//@param heightLayer - TERRAIN or OVERLAY, the layer used as the height map
	//@param offset - translation applied after scaling (e.g. to draw a second map next to the first)
	void Draw(Renderer& renderer, Camera& camera, LightSource& light, const TerrainRenderSettings& settings,
		Layer heightLayer = Layer::TERRAIN, glm::vec3 offset = glm::vec3(0.0f));

	//One map texel spans this many world units
	static constexpr float worldUnitsPerTexel = 4.0f;
	//Texture unit of the surface map of the drawn height layer; biome colors use unit 2 (Layer::BIOMES)
	static constexpr int surfaceUnit = 3;
	//Texture unit of the splat map (units 4 and 5 hold the material arrays, TerrainMaterials)
	static constexpr int splatUnit = 6;

	//The terrain shader program; ChunkWorld draws with the same shaders
	static std::unique_ptr<Shader> CreateTerrainShader();
	//Binds the terrain shader and sets the uniforms that do not change between the maps drawn in a frame
	//@param model - also gives normalMatrix; drawing several maps with one shader may change only its translation
	//@param heightUnit - texture unit of the height map
	//@param flatMin, flatMax - area (XZ, world units before modelScale) framed by the 2D map view
	//Sets useSplat to false; a map with a splat map sets it to true after binding it to splatUnit
	static void SetFrameUniforms(Shader& shader, FrameUniforms& frame, Camera& camera, LightSource& light, const TerrainRenderSettings& settings,
		const glm::mat4& model, int heightUnit, glm::vec2 flatMin, glm::vec2 flatMax);
	//The same with a ready frame block (depth passes of shadows)
	static void SetFrameUniforms(Shader& shader, FrameUniforms& frame, const FrameUniforms::Block& block, const TerrainRenderSettings& settings,
		const glm::mat4& model, int heightUnit);
	//Camera, light and fog of a frame as the terrain sees it: in the 2D map the top-down camera framing flatMin - flatMax, without fog
	static FrameUniforms::Block FrameBlock(Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, glm::vec2 flatMin, glm::vec2 flatMax);

private:
	void BuildMesh();
	//Recomputes the surface map of a height layer if the layer changed since the last time
	void UpdateSurface(Layer heightLayer);

	glm::vec2 flatMin = glm::vec2(0.0f), flatMax = glm::vec2(0.0f);

	int width = 0, height = 0;
	int mapResolution = 50;
	unsigned int stride = 5;
	std::vector<float> vertices;

	VertexBufferLayout layout;
	std::unique_ptr<VertexArray> vao;
	std::unique_ptr<VertexBuffer> vertexBuffer;
	std::unique_ptr<Shader> shader;
	std::unique_ptr<TextureClass> textures[3];
	std::unique_ptr<TextureClass> splat;

	GpuSurfacePass surfacePass;
	FrameUniforms frameUniforms;
	//Surface maps of TERRAIN and OVERLAY (GL_RGBA32F), indexed like the layers
	std::unique_ptr<TextureClass> surfaces[2];
	bool surfaceOutdated[2] = { true, true };
};
