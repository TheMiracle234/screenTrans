// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include <gl/VertexBuffer.h>
#include <gl/debug.h>

#include <cassert>

namespace gl {

	VertexBuffer::VertexBuffer(const VertexBufferLayout& layout, std::vector<uint8_t> data, BufferUsage usage) :
		m_data(std::move(data)),
		m_layout(layout)
	{
		GLCALL(glGenBuffers(1, &m_id));
		GLCALL(glBindBuffer(GL_ARRAY_BUFFER, m_id));
		m_data.shrink_to_fit();
		bool no_data = false; // to avoid data.size() == 0 error
		if (m_data.size() == 0) {
			m_data.resize(1); // force to alloc memory
			no_data = true;
		}
		GLCALL(glBufferData(GL_ARRAY_BUFFER, m_data.capacity(), nullptr, static_cast<GLenum>(usage)));
		GLCALL(glBufferSubData(GL_ARRAY_BUFFER, 0, m_data.size(), m_data.data()));
		if (no_data) {
			m_data.resize(0);
		}
	}

	VertexBuffer::~VertexBuffer() {
		GLCALL(glDeleteBuffers(1, &m_id));
	}

	VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept :
		m_id(other.m_id),
		m_data(std::move(other.m_data)),
		m_layout(std::move(other.m_layout))
	{
		other.m_id = 0;
	}

	void VertexBuffer::operator=(VertexBuffer&& other) noexcept {
		if (this == &other)
			return;
		m_id = (other.m_id);
		m_data = (std::move(other.m_data));
		m_layout = (std::move(other.m_layout));
		other.m_id = 0;
	}

	auto currentVbo() -> unsigned int{
		int vbo;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vbo);
		return (unsigned int)vbo;
	}

	void VertexBuffer::bind() const {
		GLCALL(glBindBuffer(GL_ARRAY_BUFFER, m_id));
	}

	void VertexBuffer::unbind() const {
		assert(currentVbo() == m_id);
		GLCALL(glBindBuffer(GL_ARRAY_BUFFER, 0));
	}

	void VertexBuffer::resize(size_t bytes, BufferUsage usage, bool shrink_to_fit, bool keep_data) {
		assert(currentVbo() == m_id);
		const size_t old_capacity = capacityBytes();
		m_data.resize(bytes);
		if (shrink_to_fit) {
			m_data.shrink_to_fit();
		}
		if (capacityBytes() != old_capacity) {
			const void* newdata = keep_data ? m_data.data() : nullptr;
			GLCALL(glBufferData(GL_ARRAY_BUFFER, capacityBytes(), newdata, static_cast<GLenum>(usage)));
		}
	}

	void VertexBuffer::resetData(std::vector<uint8_t> data, BufferUsage usage, bool shrink_to_fit) {
		assert(currentVbo() == m_id);
		resize(data.size(), usage, shrink_to_fit, false);
		m_data = std::move(data);
		glBufferSubData(GL_ARRAY_BUFFER, 0, m_data.size(), m_data.data());
	}

	void VertexBuffer::setData(size_t bytesOffset, const void* data, size_t bytes) {
		assert(currentVbo() == m_id);
		assert(bytesOffset + bytes <= this->bytes());
		memcpy(m_data.data() + bytesOffset, data, bytes);
		GLCALL(glBufferSubData(GL_ARRAY_BUFFER, bytesOffset, bytes, data));
	}

	void VertexBuffer::setData(size_t vertexOffset, size_t verticesCount, const void* data) {
		const size_t bytesOffset = vertexOffset * vertexBytes();
		const size_t bytes = verticesCount * vertexBytes();
		this->setData(bytesOffset, data, bytes);
	}

	void VertexBuffer::pushData(const void* data, size_t bytes, BufferUsage usage, bool shrink_to_fit) {
		assert(currentVbo() == m_id);
		assert(bytes % vertexBytes() == 0);
		this->resize(this->bytes() + bytes, usage, shrink_to_fit, true);
		this->setData(this->bytes() - bytes, data, bytes);
	}

}