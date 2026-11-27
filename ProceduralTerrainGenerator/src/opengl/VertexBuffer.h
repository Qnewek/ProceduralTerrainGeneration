#pragma once

class  VertexBuffer
{
private:
	unsigned int m_RendererID;
public:
	VertexBuffer(const void* data, unsigned int size);
	~VertexBuffer();
	VertexBuffer(const VertexBuffer&) = delete;
	VertexBuffer& operator=(const VertexBuffer&) = delete;

	void Bind() const;
	void Unbind() const;
	void UpdateData(const void* data, unsigned int size);

	unsigned int GetRendererID() const { return m_RendererID; }
};
