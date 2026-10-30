#include "TextureClass.h"

TextureClass::TextureClass(int _width, int _height, GLenum _internalFormat) :
	m_RendererID(0), m_Width(_width), m_Height(_height)
{
	glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);

	//Maps are sampled only in the TES, where texture() always reads level 0, so mipmaps would be wasted
	glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTextureStorage2D(m_RendererID, 1, _internalFormat, m_Width, m_Height);
}

TextureClass::~TextureClass()
{
	glDeleteTextures(1, &m_RendererID);
}

void TextureClass::Upload(const void* data, GLenum format, GLenum type)
{
	glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, format, type, data);
}

void TextureClass::Bind(unsigned int slot) const
{
	glBindTextureUnit(slot, m_RendererID);
}
