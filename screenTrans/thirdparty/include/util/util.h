#pragma once

#include <glm/vec4.hpp>

#include <iostream>
#include "debug/debug.h"

namespace TM {

	class Flag8 {
		uint8_t flags;
	public:
		bool operator[](int i) const {
			return flags & i;
		}
		void set(int i) {
			flags |= i;
		}
		void remove(int i) {
			flags &= ~i;
		}
	};

	inline glm::vec4 colorOf(int r, int g, int b, int a) {
		return glm::vec4(
			static_cast<float>(r) / 0xFF,
			static_cast<float>(g) / 0xFF,
			static_cast<float>(b) / 0xFF,
			static_cast<float>(a) / 0xFF
		);
	}

	inline glm::vec4 colorOf(uint32_t RGBA) {
		int r = static_cast<int>((RGBA >> 24) & 0xFF);
		int g = static_cast<int>((RGBA >> 16) & 0xFF);
		int b = static_cast<int>((RGBA >> 8) & 0xFF);
		int a = static_cast<int>(RGBA & 0xFF);
		return colorOf(r, g, b, a);
	}

	inline uint32_t rgbaOf(const glm::vec4& color) {
		uint32_t r = static_cast<uint32_t>(color.r * 0xFF) & 0xFF;
		uint32_t g = static_cast<uint32_t>(color.g * 0xFF) & 0xFF;
		uint32_t b = static_cast<uint32_t>(color.b * 0xFF) & 0xFF;
		uint32_t a = static_cast<uint32_t>(color.a * 0xFF) & 0xFF;
		return (r << 24) | (g << 16) | (b << 8) | a;
	}

	inline uint32_t rgbaOf(const glm::ivec4& color) {
		uint32_t r = static_cast<uint32_t>(color.r) & 0xFF;
		uint32_t g = static_cast<uint32_t>(color.g) & 0xFF;
		uint32_t b = static_cast<uint32_t>(color.b) & 0xFF;
		uint32_t a = static_cast<uint32_t>(color.a) & 0xFF;
		return (r << 24) | (g << 16) | (b << 8) | a;
	}

	template<typename T>
	inline T clamp(T value, T min, T max) {
		if (value < min) return min;
		if (value > max) return max;
		return value;
	}

	template<typename Ptr>
	inline Ptr cp(Ptr ptr) {
#ifndef NDEBUG
		TM_assertOr(ptr, "nullptr error");
#endif
		return ptr;
	}
}