#include "render/Shader.h"
#include "debug/debug.h"
#include "render/Data.h"
#include "component/Component.h"
#include "window/Window.h"

#include <GL/glew.h>

#include <iostream>
#include <utility>
#include <type_traits>

#include <vector>

namespace TM {

	Shader::staticVarsForTxt Shader::sv;

	void Shader::staticInit()
	{
		TM_println("shader static Init");

		// ================= txtShader part ================= 
		TM_assertOr(!FT_Init_FreeType(&sv.ft), "Failed to initialize FreeType: ");

		const char* vertexShaderSource = R"(
			#version 330 core
			layout (location = 0) in vec4 vertex; // <vec2 pos, vec2 tex>
			out vec2 TexCoords;
    
			uniform mat4 projection;

			void main() {
				gl_Position = projection * vec4(vertex.x, vertex.y, 0.0, 1.0);
				TexCoords = vertex.zw;
			}
		)";

		const char* fragmentShaderSource = R"(
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

	Shader::Shader(const Data& _data, std::string_view vertexCode, std::string_view fragmentCode, unsigned int _glDrawtype):
		data(_data), glDrawType(_glDrawtype), buffer()
	{
		TM_println("Shader construct");
		TM_assertOr(Window::glewiInited(), "construct shader before window constructed");
		init(vertexCode, fragmentCode);
	}

	void Shader::init(std::string_view vertexCode, std::string_view fragmentCode) {
		const char* vShaderCode = vertexCode.data();
		const char* fShaderCode = fragmentCode.data();

		unsigned int v, f;
		int success;
		constexpr int len = 512;
		char infoLog[len];

		v = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(v, 1, &vShaderCode, nullptr);
		glCompileShader(v);
		glGetShaderiv(v, GL_COMPILE_STATUS, &success);
		if (!success) {
			glGetShaderInfoLog(v, len, nullptr, infoLog);
			TM_perror(infoLog);
		}

		f = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(f, 1, &fShaderCode, nullptr);
		glCompileShader(f);
		glGetShaderiv(f, GL_COMPILE_STATUS, &success);
		if (!success) {
			glGetShaderInfoLog(f, len, nullptr, infoLog);
			TM_perror(infoLog);
		}

		this->id = glCreateProgram();
		glAttachShader(id, v);
		glAttachShader(id, f);
		glLinkProgram(id);

		glGetProgramiv(id, GL_LINK_STATUS, &success);
		if (!success) {
			glGetProgramInfoLog(id, len, nullptr, infoLog);
			TM_perror(infoLog);
		}

		glDeleteShader(v);
		glDeleteShader(f);

		buffer.init();
	}

	Shader::~Shader()
	{
		glDeleteProgram(id);
		TM_println("shader destruction");
	}

	void Shader::draw(Component* const comp)
	{
		const IndicesPos& indicesPos = comp->getIndicesPos();
		if(!dataLoaded) {
			loadData();
			dataLoaded = true;
		}
		glBindTexture(GL_TEXTURE_2D, comp->getTexture());
		glDrawElements(comp->getGlDrawMode(), indicesPos.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(indicesPos.offset * sizeof(unsigned int)));

		auto txt = comp->getText();
		if (txt) {
			renderText(
				comp->getFace().value(), comp->getText().value(), comp->getTextPos(), comp->getFontWidth(), comp->getFontHeight(), 1.0f, 
				comp->getTextColor().value(), comp->shader->id, comp->shader->buffer.VAO, *comp->window, comp->getViewport()
			);
		}
	}

	std::vector<uint32_t> Shader::utf8_to_unicode(std::string_view utf8_str) {
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
				TM_setError("无效UTF - 8");
				continue;
			}
			unicode_points.push_back(code);
		}
		return unicode_points;
	}

	void Shader::load_unicode(FT_Face face, uint32_t c, int w, int h)
	{
		if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
			std::cerr << "Failed to load Glyph!" << std::endl;
		}

		FT_Set_Pixel_Sizes(face, static_cast<unsigned int>(w), static_cast<unsigned int>(h)); // 设置字体大小

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
		sv.characters.insert(std::pair<uint64_t, Character>(to_u64(w,h,c), character));

	}

	int Shader::renderText(
		FT_Face face, std::string_view text, glm::vec2 pos, int width, int height, float scale,
		const glm::vec4& color, unsigned int lastShader, unsigned int lastVAO, Window& window, const Viewport vp, bool getWidth
	) {

		int outWidth = static_cast<int>(pos.x);

		float baselineY = 0;

		if (!getWidth) {

			pos.y = vp.h - pos.y - height;

			// ===================== 【修复核心】字体级固定垂直参数（像素单位，无坐标溢出）=====================
			// 1. FreeType 核心：必须把 字体设计单位 转为 像素单位（和你的Character像素匹配）
			float fontSize = static_cast<float>(height); // 你加载字体用的像素大小（和load_unicode的height一致）
			float unitsPerEM = face->units_per_EM;

			// 转为像素后的字体固定上下界（文本编辑器永久不变的值）
			float ascentPx = (face->ascender * fontSize) / unitsPerEM;   // 基线以上最大高度（像素）
			float descentPx = -(face->descender * fontSize) / unitsPerEM; // 基线以下最大高度（像素，转正数）
			float lineHeightPx = ascentPx + descentPx;                    // 字体固定行高（永久不变）

			// 2. 按钮垂直居中：计算文字在按钮内的固定基线Y坐标（绝对不会画出去）
			float buttonCenterY = pos.y + height / 2.0f;
			baselineY = buttonCenterY - (lineHeightPx / 2.0f - descentPx);
			// =============================================================================================

			glm::mat4 projection = glm::ortho(0.0f, (float)vp.w, 0.0f, (float)vp.h);
			glUseProgram(sv.txtShader);
			glUniformMatrix4fv(sv.projection, 1, GL_FALSE, glm::value_ptr(projection));

			glUseProgram(sv.txtShader);
			glUniform4f(sv.textColor, color.x, color.y, color.z, color.w);
			glActiveTexture(GL_TEXTURE0);

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
					{ xpos + w, ypos + h,   1.0, 0.0 }
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