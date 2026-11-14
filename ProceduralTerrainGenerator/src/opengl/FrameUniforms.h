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
	};
	static_assert(sizeof(Block) == 208, "FrameUniforms::Block must match FrameBlock (std140)");
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
