#include "component/Image.h"
#include "window/Window.h"
#include "render/Data.h"
#include "util/util.h"

#include <filesystem>

#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>

namespace TM {
	
	void Image::staticInit() {
		//stbi_set_flip_vertically_on_load(true);
	}

	Image::Image(
		Window& _window, int x, int y, std::string_view u8Path, int _width, int _height,
		std::unique_ptr<Component> _child, const std::shared_ptr<Shader>& _shader, bool loadData
	) :
		Component(_shader ? _shader : defaultShader(), _window, std::move(_child)),
		width(static_cast<float>(_width)), height(static_cast<float>(_height)),
		position(static_cast<float>(x), static_cast<float>(y))
	{
		int w, h;
		unsigned char* data = stbi_load(u8Path.data(), &w, &h, nullptr, 4);
		imgWidth = static_cast<float>(w);
		imgHeight = static_cast<float>(h);

		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);

		stbi_image_free(data);

		if (width == (float)DEFAULT_WIDTH && height == (float)DEFAULT_HEIGHT) {
			width = (float)w;
			height = (float)h;
		}
		else if (width == (float)DEFAULT_WIDTH && height != (float)DEFAULT_HEIGHT) {
			width = height * ((float)w / h);
		}
		else if (width != (float)DEFAULT_WIDTH && height == (float)DEFAULT_HEIGHT) {
			height = width * ((float)h / w);
		}

		if (loadData) {
			TM_COMP_INIT;
		}
	}

	Image::Image(
		Window& _window, int x, int y, const std::vector<uint8_t>& _data, int imgW, int imgH, int _width, int _height,
		std::unique_ptr<Component> _child, const std::shared_ptr<Shader>& _shader, bool loadData
	) :
		Component(_shader ? _shader : defaultShader(), _window, std::move(_child)),
		imgWidth(static_cast<float>(imgW)), imgHeight(static_cast<float>(imgH)),
		width(static_cast<float>(_width)), height(static_cast<float>(_height)),
		position(static_cast<float>(x), static_cast<float>(y))
	{
		int w = imgW, h = imgH;
		const unsigned char* data = _data.data();

		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);

		if (width == (float)DEFAULT_WIDTH && height == (float)DEFAULT_HEIGHT) {
			width = (float)w;
			height = (float)h;
		}
		else if (width == (float)DEFAULT_WIDTH && height != (float)DEFAULT_HEIGHT) {
			width = height * ((float)w / h);
		}
		else if (width != (float)DEFAULT_WIDTH && height == (float)DEFAULT_HEIGHT) {
			height = width * ((float)h / w);
		}

		if (loadData) {
			TM_COMP_INIT;
		}
	}


	Image::~Image()
	{
		glDeleteTextures(1, &texture);
		defaultShader(true);
	}

	void Image::resetData(const std::vector<uint8_t>& data, int imgW, int imgH) {

		shader->buffer.bindAll();

		glBindTexture(GL_TEXTURE_2D, texture);
		if (imgW != (int)this->imgWidth || imgH != (int)this->imgHeight) {
			this->imgWidth = static_cast<float>(imgW);
			this->imgHeight = static_cast<float>(imgH);
			glTexImage2D(
				GL_TEXTURE_2D,
				0,
				GL_RGBA8,
				(int)imgWidth,
				(int)imgHeight,
				0,
				GL_BGRA,
				GL_UNSIGNED_BYTE,
				data.data()
			);
		}
		else {
			glTexSubImage2D(
				GL_TEXTURE_2D,
				0,
				0,
				0,
				(int)imgWidth,
				(int)imgHeight,
				GL_BGRA,
				GL_UNSIGNED_BYTE,
				data.data()
			);
		}
		glBindTexture(GL_TEXTURE_2D, 0);

		shader->buffer.unbind();
	}

	void Image::resetData(const std::vector<uint8_t>& data) {
		resetData(data, (int)imgWidth, (int)imgHeight);
	}

	void Image::resize(int w, int h) {
		resize_relocate(w, h, (int)position.x, (int)position.y);
	}

	void Image::relocate(int x, int y) {
		resize_relocate((int)width, (int)(height), x, y);
	}

	void Image::resize_relocate(int w, int h, int x, int y) {
		bool needUpdate = false;

		if ((int)width != w) {
			width = (float)w;
			needUpdate = true;
		}
		if ((int)height != h) {
			height = (float)h;
			needUpdate = true;
		}

		if ((int)position.x != x) {
			position.x = (float)x;
			needUpdate = true;
		}
		if ((int)position.y != y) {
			position.y = (float)y;
			needUpdate = true;
		}

		if (!needUpdate)
			return;

		float sub_data[] = {
			position.x + width, position.y + height,	1.0f, 1.0f,
			position.x,			position.y + height,	0.0f, 1.0f,
			position.x + width, position.y,				1.0f, 0.0f,
			position.x,			position.y,				0.0f, 0.0f,
		};
		auto& data = getData();
		int offset = data.indices[indicesPos.offset] * data.structure.size;

		shader->getBuffer().bindAll();
		glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)offset, sizeof(sub_data), sub_data);
		shader->getBuffer().unbind();
	}

	std::shared_ptr<Shader> Image::defaultShader(bool destruct)
	{
		static std::shared_ptr<Shader> shader = nullptr;
		if (destruct && shader && shader.use_count() == 2) {
			shader.reset();
			return nullptr;
		}
		if (!shader) {
			const Data _data = Data(
				VertexStructure(
					{ 0, 1 },
					{ 2, 2 },
					4 * sizeof(float),
					{ 0, 2 * sizeof(float)}
				)
			);

			const std::string_view _vsrc = R"(
				#version 330 core
				layout (location = 0) in vec2 aPos;
				layout (location = 1) in vec2 aTexCoord;
				out vec2 texPos;
				uniform mat4 projection;
				void main(){
					gl_Position = projection * vec4(aPos.x, aPos.y, 0.0f, 1.0f);
					texPos = aTexCoord;
				}
			)";

			const std::string_view _fsrc = R"(
				#version 330 core
				out vec4 FragColor;
				in vec2 texPos;
				uniform sampler2D texture1;
				void main(){
					FragColor = texture(texture1, texPos);
				}
			)";

			shader = std::make_shared<Shader>(_data, _vsrc, _fsrc, GL_STATIC_DRAW);
		}
		return shader;
	}

	void Image::initVerticesIndices()
	{
		indicesPos.offset = getData().push(
			{
				position.x + width, position.y + height,	1.0f, 1.0f,
				position.x,			position.y + height,	0.0f, 1.0f,
				position.x + width, position.y,				1.0f, 0.0f,
				position.x,			position.y,				0.0f, 0.0f,
			},
			{
				0, 1, 2,
				1, 2, 3
			}
		);
	}

	void Image::initIndicesPos()
	{
		indicesPos.count = 6;
	}

	void Image::update()
	{
		auto vp = getViewport();
		glm::mat4 projection = glm::ortho(0.0f, (float)vp.w, (float)vp.h, 0.0f, -1.0f, 1.0f);
		shader->setUniform("projection", &projection);
	}

}