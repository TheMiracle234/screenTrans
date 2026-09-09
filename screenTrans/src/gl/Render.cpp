// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include "gl/Render.h"
#include "gl/debug.h"
#include "gl/Window.h"
#include "gl/IndexBuffer.h"

#include <glad/glad.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <utility>
#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


namespace gl{

	void draw(IndexBuffer& ebo, DrawMode mode, size_t offsetCount) {
		glDrawElements(static_cast<GLenum>(mode), static_cast<GLsizei>(ebo.count()), GL_UNSIGNED_INT,  reinterpret_cast<void*>(offsetCount * sizeof(unsigned int)));
	}

	void draw(IndexBuffer& ebo, DrawMode mode, size_t offsetCount, size_t countIndices) {
		assert(ebo.count() >= countIndices);
		glDrawElements(static_cast<GLenum>(mode), static_cast<GLsizei>(countIndices), GL_UNSIGNED_INT, reinterpret_cast<void*>(offsetCount * sizeof(unsigned int)));
	}

// ================= text render ================= 

	TextRenderInitGuard::TextRenderInitGuard() {
#ifndef NDEBUG
		TextRenderInitGuard::done = true;
#endif

		GLASSERTK(0 == FT_Init_FreeType(&sv.ft));
		constexpr const char* vertexShaderSource = R"(
			#version 330 core
			layout (location = 0) in vec4 vertex; // <vec2 pos, vec2 tex>
			out vec2 TexCoords;
    
			uniform mat4 projection;

			void main() {
				gl_Position = projection * vec4(vertex.x, vertex.y, 0.0, 1.0);
				TexCoords = vertex.zw;
			}
		)";

		constexpr const char* fragmentShaderSource = R"(
			#version 330 core
			in vec2 TexCoords;
			out vec4 color;
    
			uniform sampler2D text;
			uniform vec4 textColor;
    
			void main() {
				color = vec4(textColor.xyz, textColor.w * texture(text, TexCoords).r);
			}
		)";

		unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
		glCompileShader(vertexShader);

		unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
		glCompileShader(fragmentShader);

		sv.txtShader = glCreateProgram();
		glAttachShader(sv.txtShader, vertexShader);
		glAttachShader(sv.txtShader, fragmentShader);
		glLinkProgram(sv.txtShader);

		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		glUseProgram(sv.txtShader);
		sv.projection = glGetUniformLocation(sv.txtShader, "projection");
		sv.textColor = glGetUniformLocation(sv.txtShader, "textColor");
		sv.text = glGetUniformLocation(sv.txtShader, "text");

		glUseProgram(0);

		// for textRender()
		glGenVertexArrays(1, &sv.VAO);
		glGenBuffers(1, &sv.VBO);
		glBindVertexArray(sv.VAO);
		glBindBuffer(GL_ARRAY_BUFFER, sv.VBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}

	TextRenderInitGuard::~TextRenderInitGuard() {
		glDeleteProgram(sv.txtShader);
		GLASSERTK(0 == FT_Done_FreeType(sv.ft));
		GLCALL(glDeleteVertexArrays(1, &sv.VAO));
		GLCALL(glDeleteBuffers(1, &sv.VBO));
	}

	static inline std::vector<uint32_t> utf8_to_unicode(std::string_view utf8_str) {
		std::vector<uint32_t> unicode_points;
		const char* ptr = utf8_str.data();
		while (*ptr) {
			uint32_t code = 0;
			if ((*ptr & 0x80) == 0) { // 1字节 (0xxxxxxx)
				code = *ptr++;
			}
			else if ((*ptr & 0xE0) == 0xC0) { // 2字节 (110xxxxx 10xxxxxx)
				code = ((*ptr++ & 0x1F) << 6) | (*ptr++ & 0x3F);
			}
			else if ((*ptr & 0xF0) == 0xE0) { // 3字节 (1110xxxx 10xxxxxx 10xxxxxx)
				code = ((*ptr++ & 0x0F) << 12) | ((*ptr++ & 0x3F) << 6) | (*ptr++ & 0x3F);
			}
			else if ((*ptr & 0xF8) == 0xF0) { // 4字节 (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
				code = ((*ptr++ & 0x07) << 18) | ((*ptr++ & 0x3F) << 12) | ((*ptr++ & 0x3F) << 6) | (*ptr++ & 0x3F);
			}
			else {
				code = static_cast<uint32_t>('?');
				ptr++; // 无效UTF-8，跳过
				continue;
			}
			unicode_points.push_back(code);
		}
		return unicode_points;
	}

	static uint64_t to_u64(uint16_t w, uint16_t h, uint32_t c) {
		return
			(static_cast<uint64_t>(w) << 48) |
			(static_cast<uint64_t>(h) << 32) |
			(static_cast<uint64_t>(c));
	}

	struct whc { uint16_t w; uint16_t h; uint32_t c; };

	static whc to_whc(uint64_t whc) {
		return {
			static_cast<uint16_t>((whc >> 48) & 0xFFFF), // auto throw high bits
			static_cast<uint16_t>((whc >> 32) & 0xFFFF),
			static_cast<uint32_t>((whc) & 0xFFFFFFFF)
		};
	}

	int currentAlignment(){
		GLint alignment;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
		return alignment;
	}

	static inline void load_unicode(FT_Face face, uint32_t c, int w, int h)
	{
		GLASSERTK(0 == FT_Set_Pixel_Sizes(face, static_cast<unsigned int>(w), static_cast<unsigned int>(h))); // 设置字体大小
		GLASSERTK(0 == FT_Load_Char(face, c, FT_LOAD_RENDER));

		unsigned int texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RED,
			face->glyph->bitmap.width,
			face->glyph->bitmap.rows,
			0,
			GL_RED,
			GL_UNSIGNED_BYTE,
			face->glyph->bitmap.buffer
		);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		Character character = {
			texture,
			glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
			glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
			static_cast<unsigned int>(face->glyph->advance.x)
		};
		sv.characters.insert(std::pair<uint64_t, Character>(to_u64(w, h, c), character));
	}


	int drawText(FT_Face face, int texture_unit, std::string_view text, glm::vec2 pos, int width, int height, float scale,
		const glm::vec4& color, unsigned int lastShader, unsigned int lastVAO, const Viewport vp, bool getWidth
	) {
		assert(TextRenderInitGuard::done);
		assert(currentAlignment() == 1);

		int outWidth = static_cast<int>(pos.x);

		float baselineY = 0;

		if (!getWidth) {

			pos.y = vp.h - pos.y - height * scale;

			// ===================== 【修复核心】字体级固定垂直参数（像素单位，无坐标溢出）=====================
			// 1. FreeType 核心：必须把 字体设计单位 转为 像素单位（和你的Character像素匹配）
			float fontSize = static_cast<float>(height); // 你加载字体用的像素大小（和load_unicode的height一致）
			float unitsPerEM = face->units_per_EM;

			// 转为像素后的字体固定上下界（文本编辑器永久不变的值）
			float ascentPx = (face->ascender * fontSize) / unitsPerEM;   // 基线以上最大高度（像素）
			float descentPx = (- (face->descender * fontSize) / unitsPerEM); // 基线以下最大高度（像素，转正数）
			float lineHeightPx = (ascentPx + descentPx);                    // 字体固定行高（永久不变）

			// 2. 按钮垂直居中：计算文字在按钮内的固定基线Y坐标（绝对不会画出去）
			float buttonCenterY = pos.y + scale * height / 2.0f;
			baselineY = buttonCenterY - scale * (lineHeightPx / 2.0f - descentPx);
			// =============================================================================================

			glm::mat4 projection = glm::ortho(0.0f, (float)vp.w, 0.0f, (float)vp.h);
			glUseProgram(sv.txtShader);
			glUniformMatrix4fv(sv.projection, 1, GL_FALSE, glm::value_ptr(projection));

			glUseProgram(sv.txtShader);
			glUniform4f(sv.textColor, color.x, color.y, color.z, color.w);
			
			glUniform1i(sv.text, texture_unit);
			glActiveTexture(GL_TEXTURE0 + texture_unit);

			glBindVertexArray(sv.VAO);
			glBindBuffer(GL_ARRAY_BUFFER, sv.VBO);
		}

		

		auto utf8_chars = utf8_to_unicode(text);
		auto c = utf8_chars.begin();
		for (; c != utf8_chars.end(); c++) {

			auto u64 = to_u64(width, height, *c);
			if (!sv.characters.count(u64)) {
				load_unicode(face, *c, width, height);
			}
			Character ch = sv.characters[u64];

			float xpos = pos.x + ch.Bearing.x * scale;

			if (!getWidth) {

				float ypos = baselineY - (ch.Size.y - ch.Bearing.y) * scale;
				float w = ch.Size.x * scale;
				float h = ch.Size.y * scale;

				float vertices[6][4] = {
					{ xpos,     ypos + h,   0.0, 0.0 },
					{ xpos,     ypos,       0.0, 1.0 },
					{ xpos + w, ypos,       1.0, 1.0 },
					{ xpos,     ypos + h,   0.0, 0.0 },
					{ xpos + w, ypos,       1.0, 1.0 },
					{ xpos + w, ypos + h,   1.0, 0.0 },
				};

				glBindTexture(GL_TEXTURE_2D, ch.TextureID);
				glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
				glDrawArrays(GL_TRIANGLES, 0, 6);
			}

			pos.x += (ch.Advance >> 6) * scale;
		}

		if (!getWidth) {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindTexture(GL_TEXTURE_2D, 0);

			glUseProgram(lastShader);
			glBindVertexArray(lastVAO);
		}

		return static_cast<int>(pos.x) - outWidth;
	}

}