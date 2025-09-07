#pragma once

#include <GL/glew.h>

#include "TextureClass.h"
#include "VertexArray.h"
#include "IndexBuffer.h"
#include "Shader.h"

#define ASSERT(x) if (!(x)) __debugbreak();
#define GLCALL(x) GLClearError();\
	x;\
	ASSERT(GLLogCall(#x, __FILE__,__LINE__));
#include <ostream>

void GLClearError();

bool GLLogCall(const char* function, const char* file, int line);

class Renderer
{
public:
	void SetPatches(int numVertsPerPatch) {	glPatchParameteri(GL_PATCH_VERTICES, numVertsPerPatch);}

	void DrawTriangles(const VertexArray& va, const IndexBuffer& ib, const Shader& shader) const;
	void DrawTriangleStrips(const VertexArray& va, const IndexBuffer& ib, const Shader& shader, int numStrips, int numVertPerStrip) const;
	void DrawPatches(const VertexArray& va, const Shader& shader, int numPatches, int numPatchPts) const;
	void Clear(glm::vec3 color) const;
};