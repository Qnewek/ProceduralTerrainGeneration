#pragma once

#include <cstdint>

#include <GL/glew.h>
#include "glm/glm.hpp"

class Camera;
class LightSource;

//Camera, sun and fog of one render pass, for every shader through one uniform block: FrameBlock in common/frame.glsl (std140, binding 1).
//Every renderer owns one and uploads it before drawing, so passes with their own camera (2D map, impostor baking, previews) need no
//care about the others. Only for the thread owning the GL context
class FrameUniforms
{
public:
	//Must match FrameBlock field by field
	//Cascades of the sun shadow map, and its texture unit while drawing (sampler shadowMap in common/shadow.glsl)
	static constexpr int maxShadowCascades = 4;
	static constexpr int shadowUnit = 15;

	struct Block {
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 projection = glm::mat4(1.0f);
		glm::vec3 viewPos = glm::vec3(0.0f);
		float time = 0.0f;					//seconds since the start of the program (wind)
		glm::vec4 lightDirection = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);	//xyz = unit vector towards the sun
		glm::vec4 lightAmbient = glm::vec4(0.0f);
		glm::vec4 lightDiffuse = glm::vec4(0.0f);
		int32_t lightOn = 1;
		int32_t fogOn = 0;
		float fogStart = 0.0f;
		float fogDistance = 1.0f;
		//Sun shadows (ShadowMap): world space after modelScale -> shadow map [0, 1]^3 per cascade
		glm::mat4 shadowMatrices[maxShadowCascades] = {};
		glm::vec4 shadowSplits = glm::vec4(0.0f);		//far end of every cascade, as depth along the camera view
		glm::vec4 shadowTexels = glm::vec4(0.0f);		//world size of a shadow map texel of every cascade
		int32_t shadowCascades = 0;						//0 = no shadows
		float shadowStrength = 0.0f;					//share of the sunlight a shadow takes away
		//Pass into a depth map (shadows): fragment shaders only test alpha
		int32_t depthPass = 0;
		//Haze (common/fog.glsl): extinction per world unit at y = 0, falling by e every 1 / hazeFalloff world units up; 0 = none
		float hazeDensity = 0.0f;
		float hazeFalloff = 0.0f;
		float padding[3] = {};
		//Clouds (common/clouds.glsl): x = base, y = top (metres, top <= base = no clouds), z = coverage 0..1, w = strength of their shadows
		glm::vec4 cloudLayer = glm::vec4(0.0f);
		//xy = wind offset of the clouds (metres), z = world units per metre, w = density
		glm::vec4 cloudWind = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
	};
	static_assert(sizeof(Block) == 560, "FrameUniforms::Block must match FrameBlock (std140)");
	static constexpr GLuint binding = 1;

	FrameUniforms() = default;
	~FrameUniforms();
	FrameUniforms(const FrameUniforms&) = delete;
	FrameUniforms& operator=(const FrameUniforms&) = delete;

	//A pass with the given camera matrices; the sun and the fog switch from light
	static Block Make(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& viewPos, const LightSource& light, bool fogOn,
		float fogStart, float fogDistance);
	//A pass of the camera, with the fog of light up to the view distance of the camera
	static Block Make(Camera& camera, const LightSource& light);

	//Uploads the block and binds it to binding (the buffer is made on the first call)
	void Upload(const Block& block);

private:
	GLuint buffer = 0;
};
