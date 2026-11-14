#pragma once

#include <GL/glew.h>
#include "glm/glm.hpp"

#include "FrameUniforms.h"

class ChunkWorld;
class Camera;
class LightSource;
class Renderer;
class VegetationLayer;
class VegetationModels;
struct TerrainRenderSettings;

//Cascaded shadow map of the sun: the view range up to "Shadow distance" split into cascades (nearer = smaller = sharper), each an
//orthographic depth map from the sun in one layer of a depth texture array. A cascade covers the sphere around its slice of the view
//frustum and moves in whole texels, so shadows do not shimmer when the camera moves or turns. Casters toward the sun outside the slice
//are kept by extending the depth range towards the sun by the highest terrain.
//Use per frame: Prepare, then for every cascade BeginCascade + draw the casters with CascadeBlock, then Finish; afterwards Apply adds
//the shadows to the FrameBlock of the receivers (TerrainView::FrameBlock does it through TerrainRenderSettings::shadows).
//Only for the thread owning the GL context
class ShadowMap
{
public:
	ShadowMap() = default;
	~ShadowMap();
	ShadowMap(const ShadowMap&) = delete;
	ShadowMap& operator=(const ShadowMap&) = delete;

	//Prepare + every cascade with the terrain and the plants as casters + Finish; false = no shadows this frame.
	//Must come before the scene draws: the plants cull for the sun into the buffers the camera culling then reuses
	bool Render(Renderer& renderer, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, ChunkWorld& chunks,
		VegetationLayer& vegetation, VegetationModels* models);

	//Places the cascades for this frame; false if there are no shadows (off, 2D map, sun below the horizon)
	bool Prepare(Camera& camera, const LightSource& light, const TerrainRenderSettings& settings);
	int CascadeCount() const { return active ? cascadeCount : 0; }
	//projection * view of the sun for a cascade, world space after modelScale
	const glm::mat4& CascadeViewProjection(int cascade) const { return cascades[cascade].viewProjection; }
	//Frame block of a depth pass into a cascade: the sun camera, depthPass set
	FrameUniforms::Block CascadeBlock(int cascade, Camera& camera, const LightSource& light) const;
	//Renders into the layer of a cascade (cleared); saves the framebuffer and viewport of the scene on the first cascade
	void BeginCascade(int cascade);
	//Back to the scene framebuffer and viewport; binds the map to FrameUniforms::shadowUnit
	void Finish();
	//Adds the shadows of this frame to a block (nothing if there are none)
	void Apply(FrameUniforms::Block& block) const;

	//Section "Shadows" of the Display tab
	void ImGuiDraw();

private:
	struct Cascade {
		glm::mat4 viewProjection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 projection = glm::mat4(1.0f);
		float farDepth = 0.0f;		//depth along the camera view
		float texel = 0.0f;		//world size of a texel
	};
	//Sun view of the sphere around the camera frustum between depths nearDepth and farDepth
	Cascade Fit(const glm::mat4& cameraWorld, glm::vec2 tanHalf, float nearDepth, float farDepth, glm::vec3 sun, float extra) const;
	bool CreateTexture();

	bool enabled = true;
	float distanceMetres = 300.0f;
	int cascadeCount = 4;
	int resolution = 2048;
	float strength = 0.8f;
	float splitLambda = 0.8f;	//0 = equal cascades, 1 = logarithmic

	bool active = false;
	Cascade cascades[FrameUniforms::maxShadowCascades];
	GLuint texture = 0;
	GLuint framebuffer = 0;
	int textureResolution = 0;
	GLint sceneFramebuffer = 0;
	GLint sceneViewport[4] = {};
	GLboolean sceneScissor = GL_FALSE;
	bool sceneSaved = false;
};
