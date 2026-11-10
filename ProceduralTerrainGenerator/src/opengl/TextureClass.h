#pragma once

#include <GL/glew.h>

//2D texture (or 2D texture array) with immutable storage and a single mip level. The storage is allocated once in the constructor;
//new contents of the same size go through Upload, a different size needs a new texture.
class TextureClass
{
private:
	unsigned int m_RendererID;
	int m_Width, m_Height, m_Layers;
	GLenum m_InternalFormat;
public:
	//@param _internalFormat - sized internal format, e.g. GL_R32F
	//@param _layers - 1 = GL_TEXTURE_2D, more = GL_TEXTURE_2D_ARRAY with that many layers
	TextureClass(int _width, int _height, GLenum _internalFormat, int _layers = 1);
	~TextureClass();

	TextureClass(const TextureClass&) = delete;
	TextureClass& operator=(const TextureClass&) = delete;

	//Replaces the whole image (all layers) without reallocating
	//@param data - width * height pixels per layer, rows tightly packed, layer after layer
	//@param format - pixel format of data, e.g. GL_RED
	//@param type - component type of data, e.g. GL_FLOAT
	void Upload(const void* data, GLenum format, GLenum type);

	//Copies the whole image to data, waiting for the GPU
	//@param size - size of data in bytes
	void Read(GLenum format, GLenum type, int size, void* data) const;

	void Bind(unsigned int slot = 0) const;
	//Binds the texture for access from a compute shader (all layers of an array, as image2DArray)
	//@param access - GL_READ_ONLY, GL_WRITE_ONLY or GL_READ_WRITE
	void BindImage(unsigned int unit, GLenum access) const;

	//OpenGL name of the texture, e.g. for ImGui::Image
	inline unsigned int GetID() const { return m_RendererID; }
	inline int GetWidth() const { return m_Width; }
	inline int GetHeight() const { return m_Height; }
	inline int GetLayers() const { return m_Layers; }
};
