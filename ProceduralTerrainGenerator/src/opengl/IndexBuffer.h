#pragma once

class  IndexBuffer
{
private:
	unsigned int m_RendererID;
	unsigned int m_Count;
	unsigned int m_Capacity;
public:
	IndexBuffer(const unsigned int* data, unsigned int count);
	~IndexBuffer();
	IndexBuffer(const IndexBuffer&) = delete;
	IndexBuffer& operator=(const IndexBuffer&) = delete;

	void Bind() const;
	void Unbind() const;

	void UpdateData(const unsigned int* data, unsigned int count);

	inline unsigned int GetCount() const { return m_Count; }
};
