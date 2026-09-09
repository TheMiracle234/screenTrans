// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
#include <vector>
namespace gl {

	unsigned int currentVao();

	class VertexBuffer;
	class VertexArray {
	private:
		unsigned int m_id;
	public:
		VertexArray();
		~VertexArray();
		VertexArray(VertexArray&&) noexcept;
		void operator=(VertexArray&&) noexcept;
		VertexArray(const VertexArray&) = delete;
		VertexArray& operator=(const VertexArray&) = delete;
		
		auto id() const -> unsigned int { return m_id; }

		void bind() const;
		void unbind() const;
		void bindVboAttrib(int location, const VertexBuffer& vbo, int attribIndex) const;
		void bindVboAllAttribs(const std::vector<unsigned int>& locations, const VertexBuffer& vbo) const;
	};
}