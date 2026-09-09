// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
#include <vector>
#include <type_traits>

#ifndef GL_STATIC_DRAW 
#	define GL_STATIC_DRAW 0x88E4
#	define GL_DYNAMIC_DRAW 0x88E8
#endif

namespace gl {

	enum class BufferUsage : unsigned int {
		Static = GL_STATIC_DRAW,
		Dynamic = GL_DYNAMIC_DRAW,
	};

	template<typename T>
	inline std::vector<T> toVector(const void* data, size_t bytes) {
		const uint8_t* data0 = reinterpret_cast<const uint8_t*>(data);
		return std::vector<T>(reinterpret_cast<const T*>(data0), reinterpret_cast<const T*>(data0 + bytes));
	}

	template<typename T>
	struct is_vector : std::false_type {};
	template<typename T0, typename T1>
	struct is_vector<std::vector<T0, T1>> : std::true_type {};
	template<typename T>
	concept CVector = is_vector<T>::value;

	template<typename T, CVector Vec>
	inline std::vector<T> toVector(const Vec& vec) {
		return std::vector<T>(reinterpret_cast<const T*>(vec.data()), reinterpret_cast<const T*>(vec.data() + vec.size()));
	}

	template<CVector Vec>
	inline size_t bytesOf(const Vec& vec) {
		using T = typename Vec::value_type;
		return vec.size() * sizeof(T);
	}
}