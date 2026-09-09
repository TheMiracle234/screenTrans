// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
#include <vector>
#include <type_traits>
#include <glad/glad.h>
#include <cassert>
#include <boost/pfr.hpp>

namespace gl {

	struct int_2_10_10_10_rev { int val; };
	struct uint_2_10_10_10_rev { uint32_t val; };

	constexpr inline unsigned int getSizeOfType(unsigned int type)
	{
		switch (type)
		{
		case GL_BYTE:                       return sizeof(GLbyte);
		case GL_UNSIGNED_BYTE:              return sizeof(GLubyte);
		case GL_SHORT:                      return sizeof(GLshort);
		case GL_UNSIGNED_SHORT:             return sizeof(GLushort);
		case GL_INT:                        return sizeof(GLint);
		case GL_UNSIGNED_INT:               return sizeof(GLuint);
		case GL_HALF_FLOAT:                 return sizeof(GLhalf);    // 通常为 2 字节
		case GL_FLOAT:                      return sizeof(GLfloat);
		case GL_DOUBLE:                     return sizeof(GLdouble);
		case GL_INT_2_10_10_10_REV:         return sizeof(GLuint);    // 打包为 32 位
		case GL_UNSIGNED_INT_2_10_10_10_REV:return sizeof(GLuint);    // 打包为 32 位
		}
		assert(false);
		return 0;
	}

	template<typename T>
	consteval inline unsigned int getType() {
		unsigned int type = 0;
		if constexpr (std::is_same_v<T, GLbyte>)              { return GL_BYTE; }
		if constexpr (std::is_same_v<T, GLubyte>)             { return GL_UNSIGNED_BYTE; }
		if constexpr (std::is_same_v<T, GLshort>)             { return GL_SHORT; }
		if constexpr (std::is_same_v<T, GLushort>)            { return GL_UNSIGNED_SHORT; }
		if constexpr (std::is_same_v<T, GLint>)	              { return GL_INT; }
		if constexpr (std::is_same_v<T, GLuint>)              { return GL_UNSIGNED_INT; }
		if constexpr (std::is_same_v<T, GLhalf>)              { return GL_HALF_FLOAT; }
		if constexpr (std::is_same_v<T, GLfloat>)             { return GL_FLOAT; }
		if constexpr (std::is_same_v<T, GLdouble>)            { return GL_DOUBLE; }
		if constexpr (std::is_same_v<T, int_2_10_10_10_rev>)  { return GL_INT_2_10_10_10_REV;}
		if constexpr (std::is_same_v<T, uint_2_10_10_10_rev>) { return GL_UNSIGNED_INT_2_10_10_10_REV;}

		assert(false);
		return type;
	}

	struct VertexBufferElement
	{
		unsigned int type;
		unsigned int count;
		unsigned char normalized;
	};

	template<size_t N, typename T>
	inline consteval void setElems(std::array <VertexBufferElement, boost::pfr::tuple_size_v<T>>& res){
		using type_i = boost::pfr::tuple_element_t<N, T>;
		static_assert(!std::is_aggregate_v<type_i> || boost::pfr::tuple_size_v<type_i> > 0, "member must: not be aggregate or has at least one member");
		constexpr bool isAggr = std::is_aggregate_v<type_i>;
		if constexpr (isAggr) {
			res[N] = {
				getType<boost::pfr::tuple_element_t<0, type_i>>(),
				boost::pfr::tuple_size_v<type_i>,
				false,
			};
		}
		else {
			res[N] = {
				getType<type_i>(),
				1,
				false,
			};
		}
		if constexpr (N > 0)
			setElems<N - 1, T>(res);
	}

	template<typename T>
	inline consteval auto elemsOf() {
		constexpr size_t size = boost::pfr::tuple_size_v<T>;
		static_assert(size > 0, "must has at least one member");
		std::array <VertexBufferElement, size> res;
		setElems<size-1, T>(res);
		return res;
	}

	template<typename T>
	struct is_array : std::false_type {};

	template<typename T, size_t N>
	struct is_array<std::array<T, N>> : std::true_type {};

	template<typename T>
	concept CArray = is_array<T>::value;

	class VertexBufferLayout
	{
	private:
		std::vector<VertexBufferElement> m_Elements{};
		unsigned int m_Stride{0};
	public:
		VertexBufferLayout() = default;
		VertexBufferLayout(const VertexBufferLayout&) = default;
		VertexBufferLayout(VertexBufferLayout&&) noexcept = default;
		void operator=(const VertexBufferLayout& other) { m_Stride = other.m_Stride; m_Elements = other.m_Elements; }
		void operator=(VertexBufferLayout&& other) noexcept { m_Stride = other.m_Stride; m_Elements = std::move(other.m_Elements); }

		VertexBufferLayout(std::vector<VertexBufferElement> elements) :
			m_Elements(std::move(elements))
		{
			for (const auto& e : m_Elements) {
				m_Stride += e.count * getSizeOfType(e.type);
			}
		}

		template<CArray Arr>
		requires std::same_as<typename Arr::value_type, VertexBufferElement>
		VertexBufferLayout(const Arr& arr){
			m_Elements.resize(arr.size());
			for (size_t i = 0;i < arr.size();++i) {
				m_Elements[i] = arr[i];
				m_Stride += arr[i].count * getSizeOfType(arr[i].type);
			}
		}

		template<typename T>
		void push(unsigned int count, bool normalized = false)
		{
			constexpr auto type = getType<T>();
			m_Elements.push_back({ type, count, static_cast<unsigned char>(normalized) });
			m_Stride += count * getSizeOfType(type);
		}
	
		auto elements() const -> const std::vector<VertexBufferElement>& { return m_Elements; }
		auto stride() const -> unsigned int                              { return m_Stride; }
		
		uintptr_t offsetOfElem(size_t index) const {
			assert(index < m_Elements.size());
			uintptr_t res = 0;
			for (int i = 0;i < index;++i) {
				res += m_Elements[i].count * getSizeOfType(m_Elements[i].type);
			}
			return res;
		}
	};
}