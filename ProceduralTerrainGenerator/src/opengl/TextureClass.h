#pragma once

#include <GL/glew.h>

//2D texture with immutable storage and a single mip level. The storage is allocated once in the constructor;
//new contents of the same size go through Upload, a different size needs a new texture.
class TextureClass
{
private:
	unsigned int m_RendererID;
	int m_Width, m_Height;
public:
	//@param _internalFormat - sized internal format, e.g. GL_R32F
	TextureClass(int _width, int _height, GLenum _internalFormat);
	~TextureClass();

	TextureClass(const TextureClass&) = delete;
	TextureClass& operator=(const TextureClass&) = delete;

	//Replaces the whole image without reallocating
	//@param data - width * height pixels, rows tightly packed
	//@param format - pixel format of data, e.g. GL_RED
	//@param type - component type of data, e.g. GL_FLOAT
	void Upload(const void* data, GLenum format, GLenum type);

	void Bind(unsigned int slot = 0) const;

	inline int GetWidth() const { return m_Width; }
	inline int GetHeight() const { return m_Height; }
};
