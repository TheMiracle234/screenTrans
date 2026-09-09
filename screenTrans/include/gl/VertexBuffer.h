// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <vector>
#include <gl/VertexBufferLayout.h>
#include <gl/Buffer.h>

namespace gl {

	auto currentVbo() -> unsigned int;

	class VertexBuffer {
	private:
		unsigned int m_id{ 0 };
		std::vector<uint8_t> m_data{};
		VertexBufferLayout m_layout;
	public:
		VertexBuffer(const VertexBufferLayout& layout, std::vector<uint8_t> data, BufferUsage usage);
		~VertexBuffer();
		VertexBuffer(VertexBuffer&&) noexcept;
		void operator=(VertexBuffer&&) noexcept;
		VertexBuffer(const VertexBuffer&) = delete;
		VertexBuffer& operator=(const VertexBuffer&) = delete;

		void bind() const;
		void unbind() const;

		void resize(size_t bytes, BufferUsage usage = BufferUsage::Static, bool shrink_to_fit = true, bool keep_data = true);
		// this will call resize inside
		void resetData(std::vector<uint8_t> data, BufferUsage usage = BufferUsage::Static, bool shrink_to_fit = true);
		void setData(size_t bytesOffset, const void* data, size_t bytes);
		void setData(size_t vertexOffset, size_t verticesCount, const void* data);
		void pushData(const void* data, size_t bytes, BufferUsage usage = BufferUsage::Static, bool shrink_to_fit = true);

		auto id() const -> unsigned int { return m_id; }
		auto bytes() const -> size_t { return m_data.size(); }
		auto vertexBytes() const -> size_t { return m_layout.stride(); }
		auto capacityBytes() const -> size_t { return m_data.capacity(); }
		auto data() const -> const std::vector<uint8_t>& { return m_data; }
		auto layout() const -> const VertexBufferLayout& { return m_layout; }
		auto verticesCount() const -> size_t { return bytes() / m_layout.stride(); }
	};
}