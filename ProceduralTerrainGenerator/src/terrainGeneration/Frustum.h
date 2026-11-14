#pragma once

#include "glm/glm.hpp"

//View frustum as six planes, for culling boxes on the CPU. Same test as the patch culling in Terrain_controlTes.shader
class Frustum
{
public:
	//@param viewProjection - projection * view (* model for boxes given in model space), OpenGL clip space (depth -1 to 1)
	explicit Frustum(const glm::mat4& viewProjection)
	{
		//Gribb-Hartmann: a point is inside when -w <= x, y, z <= w; every inequality is a plane row3 +- row_i
		const glm::mat4 m = glm::transpose(viewProjection);
		for (int i = 0; i < 3; i++) {
			planes[i * 2] = m[3] + m[i];
			planes[i * 2 + 1] = m[3] - m[i];
		}
	}

	//@return false only if the box lies entirely outside one of the planes; a box near a corner may pass although outside
	bool Intersects(const glm::vec3& boxMin, const glm::vec3& boxMax) const
	{
		for (const glm::vec4& plane : planes) {
			//Corner of the box farthest along the plane normal
			const glm::vec3 corner(plane.x >= 0.0f ? boxMax.x : boxMin.x, plane.y >= 0.0f ? boxMax.y : boxMin.y, plane.z >= 0.0f ? boxMax.z : boxMin.z);
			if (glm::dot(glm::vec3(plane), corner) + plane.w < 0.0f) {
				return false;
			}
		}
		return true;
	}

	//The six planes, for the same test on the GPU
	const glm::vec4* Planes() const { return planes; }

private:
	glm::vec4 planes[6];	//left, right, bottom, top, near, far; (normal, distance), inside where dot >= 0
};
