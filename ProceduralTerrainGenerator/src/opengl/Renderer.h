#pragma once

#include <GL/glew.h>

#include "TextureClass.h"
#include "VertexArray.h"
#include "IndexBuffer.h"
#include "Shader.h"

#ifndef NDEBUG
#ifdef _MSC_VER
#define ASSERT(x) if (!(x)) __debugbreak();
#else
#include <cassert>
#define ASSERT(x) assert(x)
#endif
#define GLCALL(x) do { GLClearError(); x; ASSERT(GLLogCall(#x, __FILE__, __LINE__)); } while (0)
#else
#define ASSERT(x) ((void)0)
#define GLCALL(x) x
#endif
#include <ostream>

void GLClearError();

bool GLLogCall(const char* function, const char* file, int line);

class Renderer
{
public:
	void SetPatches(int numVertsPerPatch) {	glPatchParameteri(GL_PATCH_VERTICES, numVertsPerPatch);}

	void DrawTriangles(const VertexArray& va, const IndexBuffer& ib, const Shader& shader) const;
	void DrawPatches(const VertexArray& va, const Shader& shader, int numPatches, int numPatchPts) const;
	void Clear(glm::vec3 color) const;
};