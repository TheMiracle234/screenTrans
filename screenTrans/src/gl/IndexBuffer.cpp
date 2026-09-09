// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include <glad/glad.h>
#include <cassert>
#include <gl/IndexBuffer.h>
#include <gl/VertexArray.h>
#include <gl/debug.h>

namespace gl {

	unsigned int currentEbo() {
		int ebo;
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);
		return ebo;
	}

	IndexBuffer::IndexBuffer(std::vector<unsigned int> data, BufferUsage usage) :
		m_data(std::move(data))
	{
		GLCALL(glGenBuffers(1, &m_id));
		GLCALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_id));
		m_data.shrink_to_fit();
		bool no_data = false;
		if (m_data.size() == 0) {
			m_data.resize(1);
			no_data = true;
		}
		GLCALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * m_data.capacity(), nullptr, static_cast<GLenum>(usage)));
		GLCALL(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(unsigned int) * m_data.size(), m_data.data()));
		if (no_data) {
			m_data.resize(0);
		}
	}

	IndexBuffer::~IndexBuffer() {
		GLCALL(glDeleteBuffers(1, &m_id));
	}

	IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept :
		m_id(other.m_id),
		m_data(std::move(other.m_data))
	{
		other.m_id = 0;
	}

	void IndexBuffer::operator=(IndexBuffer&& other) noexcept {
		if (this == &other)
			return;
		m_id = (other.m_id);
		m_data = (std::move(other.m_data));
		other.m_id = 0;
	}

	void IndexBuffer::bind(const VertexArray& vao) const {
		assert(gl::currentVao() == vao.id());
		GLCALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_id));
	}

	void IndexBuffer::unbind() const {
		assert(currentEbo() == m_id);
		GLCALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
	}

	void IndexBuffer::resize(size_t count, BufferUsage usage, bool shrink_to_fit, bool keep_data) {
		assert(currentEbo() == m_id);
		const size_t old_capacityCount = capacityCount();
		m_data.resize(count);
		if (shrink_to_fit) {
			m_data.shrink_to_fit();
		}

		if (old_capacityCount != capacityCount()) {
			const unsigned int* newdata = keep_data ? m_data.data() : nullptr;
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * m_data.capacity(), newdata, static_cast<GLenum>(usage));
		}
	}

	void IndexBuffer::resetData(std::vector<unsigned int> data, BufferUsage usage, bool shrink_to_fit) {
		assert(currentEbo() == m_id);
		resize(data.size(), usage, shrink_to_fit, true);
		m_data = std::move(data);
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(unsigned int) * m_data.size(), m_data.data());
	}

	void IndexBuffer::setData(size_t countOffset, const unsigned int* data, size_t count) {
		assert(currentEbo() == m_id);
		assert(countOffset + count <= this->count());
		memcpy(m_data.data() + countOffset, data, count * sizeof(unsigned int));
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, countOffset * sizeof(unsigned int), count * sizeof(unsigned int), data);
	}

	void IndexBuffer::pushData(const unsigned int* data, size_t count, size_t offsetIndex, BufferUsage usage, bool shrink_to_fit) {
		assert(currentEbo() == m_id);
		const unsigned int* target_data = data;
		std::vector<unsigned int> copy_data;
		if (offsetIndex != 0) {
			copy_data = std::vector<unsigned int>(data, data + count);
			for (auto& d : copy_data) {
				 d += static_cast<unsigned int>(offsetIndex);
			}
			target_data = copy_data.data();
		}
		this->resize(this->count() + count, usage, shrink_to_fit, true);
		this->setData(this->count() - count, target_data, count);
	}
}