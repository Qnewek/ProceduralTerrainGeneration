#pragma once

#include <memory>

#include <GL/glew.h>
#include "glm/glm.hpp"

#include "FrameUniforms.h"

class Camera;
class ChunkWorld;
class LightSource;
class Shader;
class TerrainMaterials;
struct TerrainRenderSettings;

//Grass near the camera on the infinite world. Every frame grass_compute.shader goes over the map texels around the camera on each ground
//chunk and appends the blades that grow there (by the materials of the splat map, TerrainMaterial::grass) and are in view to one buffer;
//one indirect draw then builds every blade from its vertex index, with wind in the vertex shader. Nothing is kept between frames and
//nothing is read back, so the grass follows the terrain and its materials at once. Without biomes there is no splat map and no grass.
//Only for the thread owning the GL context
class GrassLayer
{
public:
	GrassLayer();
	~GrassLayer();
	GrassLayer(const GrassLayer&) = delete;
	GrassLayer& operator=(const GrassLayer&) = delete;

	//Places and draws the blades of this frame; nothing in the 2D map
	void Draw(const ChunkWorld& chunks, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings,
		const TerrainMaterials& materials);
	//Settings and statistics, in the Vegetation tab
	void ImGuiDraw();
	bool ReloadShader();

	void SetEnabled(bool on) { enabled = on; }
	//Blades drawn by the frame before the last Draw (the count comes back from the GPU a frame late)
	int DrawnBlades() const { return drawnBlades; }

	//Blades in the buffer at most
	static constexpr GLuint capacity = 1u << 20;
	//Vertices of a blade: pairs at 4 heights and the tip (SEGMENTS in grass_vertex.shader)
	static constexpr GLuint bladeVertices = 9;

private:
	bool Initialize();

	bool enabled = true;
	float density = 12.0f;		//blades per m² on full grass
	float range = 60.0f;		//metres
	float bladeHeight = 0.45f;	//metres
	float bladeWidth = 0.05f;	//metres
	float windStrength = 0.35f;
	float windSpeed = 1.5f;
	float windAngle = 30.0f;	//degrees, from +X towards +Z

	std::unique_ptr<Shader> placeShader, drawShader;
	GLuint bladeBuffer = 0, commandBuffer = 0, statsBuffer = 0, vao = 0;
	FrameUniforms frameUniforms;
	bool failed = false, statsPending = false;
	int drawnBlades = 0;
};
