#include "TextureClass.h"

TextureClass::TextureClass(int _width, int _height, GLenum _internalFormat, int _layers) :
	m_RendererID(0), m_Width(_width), m_Height(_height), m_Layers(_layers), m_InternalFormat(_internalFormat)
{
	glCreateTextures(m_Layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, 1, &m_RendererID);

	//Maps are sampled only in the TES, where texture() always reads level 0, so mipmaps would be wasted
	glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (m_Layers > 1) {
		glTextureStorage3D(m_RendererID, 1, _internalFormat, m_Width, m_Height, m_Layers);
	}
	else {
		glTextureStorage2D(m_RendererID, 1, _internalFormat, m_Width, m_Height);
	}
}

TextureClass::~TextureClass()
{
	glDeleteTextures(1, &m_RendererID);
}

void TextureClass::Upload(const void* data, GLenum format, GLenum type)
{
	if (m_Layers > 1) {
		glTextureSubImage3D(m_RendererID, 0, 0, 0, 0, m_Width, m_Height, m_Layers, format, type, data);
	}
	else {
		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, format, type, data);
	}
}

void TextureClass::Read(GLenum format, GLenum type, int size, void* data) const
{
	glGetTextureImage(m_RendererID, 0, format, type, size, data);
}

void TextureClass::Bind(unsigned int slot) const
{
	glBindTextureUnit(slot, m_RendererID);
}

void TextureClass::BindImage(unsigned int unit, GLenum access) const
{
	glBindImageTexture(unit, m_RendererID, 0, m_Layers > 1 ? GL_TRUE : GL_FALSE, 0, access, m_InternalFormat);
}
