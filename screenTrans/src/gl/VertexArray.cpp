// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include <gl/VertexArray.h>
#include <gl/VertexBuffer.h>
#include <gl/VertexBufferLayout.h>
#include <gl/debug.h>
#include <glad/glad.h>

namespace gl {
	unsigned int currentVao() {
		int vao;
		GLCALL(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao));
		return (unsigned int)vao;
	}

	VertexArray::VertexArray() {
		GLCALL(glGenVertexArrays(1, &m_id));
		bind();
	}

	VertexArray::~VertexArray() {
		GLCALL(glDeleteVertexArrays(1, &m_id));
	}

	VertexArray::VertexArray(VertexArray&& other) noexcept :
		m_id(other.m_id)
	{
		other.m_id = 0;
	}

	void VertexArray::operator=(VertexArray&& other) noexcept {
		if (this == &other)
			return;
		m_id = (other.m_id);
		other.m_id = 0;
	}

	void VertexArray::bind() const {
		GLCALL(glBindVertexArray(m_id));
	}

	void VertexArray::unbind() const {
		assert(currentVao() == m_id);
		GLCALL(glBindVertexArray(0));
	}

	void VertexArray::bindVboAttrib(int location, const VertexBuffer& vbo, int attribIndex) const {
		assert(currentVao() == m_id);
		assert(attribIndex < vbo.layout().elements().size());
		assert(currentVbo() == vbo.id());
		const auto& elem = vbo.layout().elements()[attribIndex];
		GLCALL(glVertexAttribPointer(
			location, elem.count, elem.type, elem.normalized, 
			vbo.layout().stride(), reinterpret_cast<void*>(vbo.layout().offsetOfElem(attribIndex))
		));
		glEnableVertexAttribArray(location);
	}

	void VertexArray::bindVboAllAttribs(const std::vector<unsigned int>& locations, const VertexBuffer& vbo) const {
		assert(currentVao() == m_id);
		assert(currentVbo() == vbo.id());
		assert(locations.size() == vbo.layout().elements().size());
		for (size_t i = 0;i < vbo.layout().elements().size();++i) {
			bindVboAttrib(locations[i], vbo, static_cast<int>(i));
		}
	}
}