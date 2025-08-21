#include "Renderer.h"
#include <iostream>

void GLClearError() {
    while (glGetError());
}

bool GLLogCall(const char* function, const char* file, int line) {
    while (GLenum error = glGetError()) {
        std::cout << "[OpenGL Error] (" << error << ")" << function <<
            " " << file << ":" << line << std::endl;
        return false;
    }
    return true;
}


void Renderer::Draw(const VertexArray& va, const IndexBuffer& ib, const Shader& shader) const {
	shader.Bind();
	va.Bind();
	ib.Bind();

	GLCALL(glDrawElements(GL_TRIANGLES, ib.GetCount(), GL_UNSIGNED_INT, nullptr));
}
void Renderer::DrawTriangleStrips(const VertexArray& va, const IndexBuffer& ib, const Shader& shader, int numStrips, int numVertPerStrip) const {
    shader.Bind();
    va.Bind();
    ib.Bind();

    for(unsigned int i = 0; i < numStrips; i++) {
        GLCALL(glDrawElements(GL_TRIANGLE_STRIP, numVertPerStrip, GL_UNSIGNED_INT, (void*)(sizeof(unsigned) * (numVertPerStrip) * i)));
	}
}


void Renderer::Clear(glm::vec3 color) const {
	glClearColor(color.x, color.y, color.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}