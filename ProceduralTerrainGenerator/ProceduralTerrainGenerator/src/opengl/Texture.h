#pragma once

#include "Renderer.h"

class Texture
{
	private:
	unsigned int m_RendererID;
	std::string m_FilePath;
	unsigned char* m_LocalBuffer;
	int m_Width, m_Height, m_BPP;
public:
	Texture();
	Texture(const std::string& path);
	Texture(unsigned int width, unsigned int height, unsigned char* image);
	~Texture();

	void Bind(unsigned int slot = 0) const;
	void Unbind() const;

	inline int GetWidth() const { return m_Width; }
	inline int GetHeight() const { return m_Height; }

	void SetNewImage(unsigned char* image);
	void SetNewImage(const std::string& path);
};
