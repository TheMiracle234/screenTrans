// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <vector>
#include <gl/Buffer.h>

namespace gl {

	class VertexArray;

	unsigned int currentEbo();

	class IndexBuffer {
	private:
		unsigned int m_id{0};
		std::vector<unsigned int> m_data{};
	public:
		IndexBuffer(std::vector<unsigned int> data, BufferUsage usage);
		~IndexBuffer();
		IndexBuffer(const IndexBuffer&) = delete;
		IndexBuffer& operator=(const IndexBuffer&) = delete;
		IndexBuffer(IndexBuffer&&) noexcept;
		void operator=(IndexBuffer&&) noexcept;

		auto id() const -> unsigned int	                      { return m_id; }
		auto data() const -> const std::vector<unsigned int>& { return m_data; }
		auto count() const -> size_t                          { return m_data.size(); }
		auto bytes() const -> size_t                          { return count() * sizeof(unsigned int); }
		auto capacityCount() const -> size_t                  { return m_data.capacity(); }
		auto capacityBytes() const -> size_t                  { return capacityCount() * sizeof(unsigned int); }

		void bind(const VertexArray& vao) const;
		void unbind() const;

		void resize(size_t count, BufferUsage usage = BufferUsage::Static, bool shrink_to_fit = true, bool keep_data = true);
		// this will call resize inside
		void resetData(std::vector<unsigned int> data, BufferUsage usage = BufferUsage::Static, bool shrink_to_fit = true);
		void setData(size_t countOffset, const unsigned int* data, size_t count);
		void pushData(const unsigned int* data, size_t count, size_t offsetIndex, BufferUsage usage = BufferUsage::Static, bool shrink_to_fit = true);
	};
}