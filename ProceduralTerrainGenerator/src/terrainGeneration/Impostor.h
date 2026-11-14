#pragma once

#include <algorithm>
#include <cmath>

#include "glm/glm.hpp"

//Octahedral impostors of the upper hemisphere: a model is rendered from frames x frames directions, each into one cell of an atlas.
//Directions map to the square [0, 1]^2 so that neighbouring cells look from neighbouring directions; frame (i, j) looks from
//HemiOctDecode((i, j) / (frames - 1)). A view direction then blends the four frames around its grid position.
//common/impostor.glsl has the same functions for the shaders: a change in one needs the same change in the other.
namespace impostor
{
	//Unit direction of the upper hemisphere (y >= 0) to [0, 1]^2: the horizon on the border of the square, straight up in its middle
	inline glm::vec2 HemiOctEncode(glm::vec3 d)
	{
		const glm::vec2 p = glm::vec2(d.x, d.z) / (std::abs(d.x) + std::abs(d.y) + std::abs(d.z));
		return glm::vec2(p.x + p.y, p.x - p.y) * 0.5f + 0.5f;
	}

	//Inverse of HemiOctEncode: a unit direction with y >= 0
	inline glm::vec3 HemiOctDecode(glm::vec2 uv)
	{
		const glm::vec2 h = uv * 2.0f - 1.0f;
		const glm::vec2 p = glm::vec2(h.x + h.y, h.x - h.y) * 0.5f;
		return glm::normalize(glm::vec3(p.x, 1.0f - std::abs(p.x) - std::abs(p.y), p.y));
	}

	//Direction frame (i, j) of a frames x frames atlas looks from (towards the model)
	inline glm::vec3 FrameDirection(int i, int j, int frames)
	{
		return HemiOctDecode(glm::vec2(static_cast<float>(i), static_cast<float>(j)) / static_cast<float>(frames - 1));
	}

	//Axes of the image seen from direction d (unit, pointing from the model to the viewer): right and up, perpendicular to d,
	//up as close to +y as possible; straight from above right = +x
	inline void FrameAxes(glm::vec3 d, glm::vec3& right, glm::vec3& up)
	{
		const glm::vec3 r = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), d);
		const float length = glm::length(r);
		right = length > 1e-4f ? r / length : glm::vec3(1.0f, 0.0f, 0.0f);
		up = glm::cross(d, right);
	}
}
