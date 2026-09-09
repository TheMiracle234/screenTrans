// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <cstdint>
#include <unordered_map>
#include <string_view>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>

#ifndef GL_POINTS
#	define GL_POINTS 0x0000
#	define GL_LINES 0x0001
#	define GL_LINE_LOOP 0x0002
#	define GL_LINE_STRIP 0x0003
#	define GL_TRIANGLES 0x0004
#	define GL_TRIANGLE_STRIP 0x0005
#	define GL_TRIANGLE_FAN 0x0006
#endif

typedef struct FT_FaceRec_* FT_Face;
typedef struct FT_LibraryRec_* FT_Library;

namespace gl {
	enum class DrawMode : unsigned int{
		POINTS = GL_POINTS,
		LINES = GL_LINES,
		LINE_LOOP = GL_LINE_LOOP,
		LINE_STRIP = GL_LINE_STRIP,
		TRIANGLES = GL_TRIANGLES,
		TRIANGLE_STRIP = GL_TRIANGLE_STRIP,
		TRIANGLE_FAN = GL_TRIANGLE_FAN,
	};

	class IndexBuffer;

	void draw(IndexBuffer& ebo, DrawMode mode, size_t offsetCount = 0);
	void draw(IndexBuffer& ebo, DrawMode mode, size_t offsetCount, size_t countIndices);

	struct TextRenderInitGuard {
#ifndef NDEBUG
		inline static bool done = false;
#endif
		TextRenderInitGuard();
		~TextRenderInitGuard();
	};

	struct Viewport;

	struct Character {
		unsigned int TextureID;	// 字形纹理ID
		glm::ivec2 Size;		// 字形尺寸
		glm::ivec2 Bearing;		// 字形相对于基线的偏移
		unsigned int Advance;   // 下一个字形的偏移量
	};

	inline struct staticVarsForTxt {
		unsigned int txtShader;
		FT_Library ft;
		std::unordered_map<uint64_t, Character> characters;// 16: width | 16: height | 32: character
		unsigned int VAO, VBO;
		// uniform location
		unsigned int projection;
		unsigned int textColor;
		unsigned int text;
	} sv; // static vars

	// always set these before render text:
	//glEnable(GL_BLEND);
	//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	int drawText(FT_Face face, int texture_unit, std::string_view text, glm::vec2 pos, int width, int height, float scale,
		const glm::vec4& color, unsigned int lastShader, unsigned int lastVAO, const Viewport vp, bool getWidth = false);
}