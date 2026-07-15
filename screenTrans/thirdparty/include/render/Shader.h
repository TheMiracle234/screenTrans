#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <tuple>
#include <vector>

#include <GL/glew.h>

#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "render/Data.h"
#include "render/Buffer.h"
#include "debug/debug.h"

namespace TM {

	struct IndicesPos;
	class Component;
	class Window;
	struct Viewport;
	
	// actually renderer
	class Shader
	{
	TM_public:
		bool dataLoaded = false;
		unsigned int id;

	TM_private:
		unsigned int glDrawType;
		Data data;
		Buffer buffer;
		std::unordered_map<std::string, int> uniformLocations;

	TM_private:
		template< typename func, typename ...Args>
		std::enable_if_t<std::is_invocable_v<func, int, Args...>, void> //void
		inline shaderSetUniform(const func& glUniform, const std::string& name, Args&&... args);

	TM_public:
		Shader() = default;
		explicit Shader(const Data& _data, std::string_view vertexCode, std::string_view fragmentCode, unsigned int _glDrawtype);
		void init(std::string_view vertexCode, std::string_view fragmentCode);
		~Shader();
		void bind() { glUseProgram(id); buffer.bind(); }
		void draw(Component * const comp);
		unsigned int getVAO() { return buffer.VAO; }
		Data& getData() { return data; }
		Buffer& getBuffer() { return buffer; }
		void loadData() { buffer.setDataOf(data, glDrawType); dataLoaded = true; }
		void setUniform(const std::string& name, const int* v, int count = 1)		{ shaderSetUniform(glUniform1iv, name, count, v); }
		void setUniform(const std::string& name, const float* v, int count = 1)		{ shaderSetUniform(glUniform1fv, name, count, v); }
		void setUniform(const std::string& name, const glm::vec2* v, int count = 1) { shaderSetUniform(glUniform2fv, name, count, &(*v)[0]); }
		void setUniform(const std::string& name, const glm::vec4* v, int count = 1) { shaderSetUniform(glUniform4fv, name, count, &(*v)[0]); }
		void setUniform(const std::string& name, const glm::mat4* v, int count = 1) { shaderSetUniform(glUniformMatrix4fv, name, count, GL_FALSE, &(*v)[0][0]); }
		// if getWidth, no render and return the txt width
		int renderText(
			FT_Face face, std::string_view text, glm::vec2 pos, int width, int height, float scale,
			const glm::vec4& color, unsigned int lastShader, unsigned int lastVAO, Window& window, const Viewport vp, bool getWidth = false
		);

	//static
	TM_private:
		struct Character {
			unsigned int TextureID;	// 字形纹理ID
			glm::ivec2 Size;		// 字形尺寸
			glm::ivec2 Bearing;		// 字形相对于基线的偏移
			unsigned int Advance;   // 下一个字形的偏移量
		};

	TM_public:
		static struct staticVarsForTxt{
			unsigned int txtShader;
			FT_Library ft;
			std::unordered_map<uint64_t, Character> characters;// 16: width | 16: height | 32: character
			unsigned int VAO, VBO;
			// uniform location
			unsigned int projection;
			unsigned int textColor;
		} sv; // static vars

		struct whc { uint16_t w; uint16_t h; uint32_t c; };

	TM_public:
		static void staticInit();
		static void load_unicode(FT_Face face, uint32_t c, int w, int h);
		static std::vector<uint32_t> utf8_to_unicode(std::string_view utf8_str);

	TM_private:
		static uint64_t to_u64(uint16_t w, uint16_t h, uint32_t c) {
			return
				(static_cast<uint64_t>(w) << 48) |
				(static_cast<uint64_t>(h) << 32) |
				(static_cast<uint64_t>(c));
		}

		static whc to_whc(uint64_t whc) {
			return {
				static_cast<uint16_t>((whc >> 48) & 0xFFFF), // auto throw high bits
				static_cast<uint16_t>((whc >> 32) & 0xFFFF),
				static_cast<uint32_t>((whc		) & 0xFFFFFFFF)
			};
		}
	};

	// ================== implementations ==================
	template< typename func, typename ...Args>
	std::enable_if_t<std::is_invocable_v<func, int, Args...>, void>
	inline Shader::shaderSetUniform(const func& glUniform, const std::string& name, Args&&... args) {
		if (uniformLocations.count(name) != 0) {
			glUniform(uniformLocations[name], std::forward<Args>(args)...);
			return;
		}
		int location = uniformLocations[name] = glGetUniformLocation(id, name.data());
		TM_assertOr(location != -1, "Shader " << id << ", Uniform " << std::string(name) << " not found error");
		glUniform(location, std::forward<Args>(args)...);
	}

}