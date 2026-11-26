#include "AsyncRead.h"

#include <cstring>

AsyncRead::~AsyncRead()
{
	Cancel();
	if (buffer) {
		glDeleteBuffers(1, &buffer);
	}
}

void AsyncRead::BindBuffer(size_t bytes)
{
	Cancel();
	if (!buffer || capacity < bytes) {
		if (buffer) {
			glDeleteBuffers(1, &buffer);
		}
		glCreateBuffers(1, &buffer);
		glNamedBufferStorage(buffer, static_cast<GLsizeiptr>(bytes), nullptr, GL_MAP_READ_BIT);
		capacity = bytes;
	}
	started = bytes;
	glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
}

void AsyncRead::Fence()
{
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	//Sends the commands, so the fence is reached without anyone waiting on it
	glFlush();
}

void AsyncRead::StartTexture(const TextureClass& texture, GLenum format, GLenum type, size_t bytes)
{
	BindBuffer(bytes);
	//With a pack buffer bound the pointer is an offset into it
	glGetTextureImage(texture.GetID(), 0, format, type, static_cast<GLsizei>(bytes), nullptr);
	Fence();
}

void AsyncRead::StartPixels(glm::ivec2 pixel, glm::ivec2 size, GLenum format, GLenum type, size_t bytes)
{
	BindBuffer(bytes);
	glReadPixels(pixel.x, pixel.y, size.x, size.y, format, type, nullptr);
	Fence();
}

bool AsyncRead::Ready()
{
	if (!fence) {
		return false;
	}
	const GLenum state = glClientWaitSync(fence, 0, 0);
	if (state == GL_WAIT_FAILED) {
		//The fence will never signal: the read is dropped, so the caller can start a new one
		Cancel();
		return false;
	}
	return state == GL_ALREADY_SIGNALED || state == GL_CONDITION_SATISFIED;
}

bool AsyncRead::Take(void* out, size_t bytes)
{
	if (!Ready() || bytes != started) {
		return false;
	}
	const void* data = glMapNamedBufferRange(buffer, 0, static_cast<GLsizeiptr>(bytes), GL_MAP_READ_BIT);
	const bool mapped = data != nullptr;
	if (mapped) {
		std::memcpy(out, data, bytes);
		glUnmapNamedBuffer(buffer);
	}
	Cancel();
	return mapped;
}

void AsyncRead::Cancel()
{
	if (fence) {
		glDeleteSync(fence);
		fence = nullptr;
	}
}
