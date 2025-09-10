#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <iostream>
#include <vector>

class TextureClass
{
	private:
	unsigned int m_RendererID;
	std::string m_FilePath;
	unsigned char* m_LocalBuffer;
	int m_Width, m_Height, m_BPP;
public:
	TextureClass();
	TextureClass(const std::string& path);
	TextureClass(unsigned int width, unsigned int height, unsigned char* image);
	TextureClass(float* data, unsigned int width, unsigned int height);
	TextureClass(std::vector<glm::vec3> colorData, unsigned int width, unsigned int height);
	~TextureClass();

	void Bind(unsigned int slot = 0) const;
	void Unbind() const;

	inline int GetWidth() const { return m_Width; }
	inline int GetHeight() const { return m_Height; }

	void SetNewImage(unsigned char* image);
	void SetNewImage(const std::string& path);
};
