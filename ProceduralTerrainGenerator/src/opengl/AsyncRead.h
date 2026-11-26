#pragma once

#include <cstddef>

#include <GL/glew.h>
#include "glm/glm.hpp"

#include "TextureClass.h"

//A read of the GPU that does not stall: the data is copied into a pixel buffer behind the commands already sent, and a fence tells when it
//is there. Start*, then Ready() in later frames, then Take(). Only for the thread owning the GL context.
class AsyncRead
{
public:
	AsyncRead() = default;
	~AsyncRead();
	AsyncRead(const AsyncRead&) = delete;
	AsyncRead& operator=(const AsyncRead&) = delete;

	//Level 0 of a texture, bytes long in format / type; drops a read still pending
	void StartTexture(const TextureClass& texture, GLenum format, GLenum type, size_t bytes);
	//A rectangle of the framebuffer bound for reading (origin at the bottom left, as glReadPixels); drops a read still pending
	void StartPixels(glm::ivec2 pixel, glm::ivec2 size, GLenum format, GLenum type, size_t bytes);
	//A read was started and not taken or cancelled
	bool Pending() const { return fence != nullptr; }
	//The data has arrived (never waits); a fence that failed drops the read (Pending() becomes false)
	bool Ready();
	//Copies the data out and ends the read; false if it is not ready or bytes differs from the started size
	bool Take(void* out, size_t bytes);
	void Cancel();

private:
	//A buffer of at least bytes, bound to GL_PIXEL_PACK_BUFFER
	void BindBuffer(size_t bytes);
	void Fence();

	GLuint buffer = 0;
	size_t capacity = 0, started = 0;
	GLsync fence = nullptr;
};
