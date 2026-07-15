#include "component/Rectangle.h"
#include "window/Window.h"
#include "render/Data.h"
#include "util/util.h"

#include <glm/gtc/matrix_transform.hpp>

namespace TM {
	
	Rectangle::Rectangle(
		int x, int y, int _width, int _height, Window& _window, uint32_t RGBA,
		std::unique_ptr<Component> _child, const std::shared_ptr<Shader>& _shader, bool loadData
	):
		Component(_shader ? _shader : defaultShader(), _window, std::move(_child)),
		width(static_cast<float>(_width)), height(static_cast<float>(_height)),
		position(static_cast<float>(x), static_cast<float>(y)),rectColor(colorOf(RGBA))
	{
		if (loadData) {
			TM_COMP_INIT;
		}
	}

	Rectangle::~Rectangle()
	{
		defaultShader(true);
	}

	std::shared_ptr<Shader> Rectangle::defaultShader(bool destruct)
	{
		static std::shared_ptr<Shader> shader = nullptr;
		if (destruct && shader && shader.use_count() == 2) {
			shader.reset();
			return nullptr;
		}
		if (!shader) {
			const Data _data = Data(
				VertexStructure(
					{ 0 },
					{ 2 },
					2 * sizeof(float),
					{ 0 }
				)
			);

			const std::string_view _vsrc = R"(
				#version 330 core
				layout (location = 0) in vec2 aPos;
				uniform mat4 projection;
				void main(){
					gl_Position = projection * vec4(aPos.x, aPos.y, 0.0f, 1.0f);
				}
			)";

			const std::string_view _fsrc = R"(
				#version 330 core
				uniform vec4 uColor;
				out vec4 FragColor;
				void main(){
					FragColor = uColor;
				}
			)";

			shader = std::make_shared<Shader>(_data, _vsrc, _fsrc, GL_STATIC_DRAW);
		}
		return shader;
	}

	void Rectangle::resize(int w, int h) {
		shader->getBuffer().bindAll();

		bool needUpdate = false;
		if ((int)width != w) {
			width = (float)w;
			needUpdate = true;
		}
		if ((int)height != h) {
			height = (float)h;
			needUpdate = true;
		}

		if (!needUpdate)
			return;

		float sub_data[] = {
			position.x + width, position.y + height,
			position.x,			position.y + height,
			position.x + width,	position.y,
			position.x,			position.y,
		};
		auto& data = getData();
		int offset = data.indices[indicesPos.offset] * data.structure.size;
		TM_println("offset: " << indicesPos.offset);

		glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)offset, sizeof(sub_data), sub_data);
		shader->getBuffer().unbind();
	}

	void Rectangle::initVerticesIndices()
	{
		indicesPos.offset = getData().push(
			{
				position.x + width, position.y + height,
			    position.x,			position.y + height,
				position.x + width,	position.y,
			    position.x,			position.y,
			},
			{
				0, 1, 2,
				1, 2, 3
			}
		);
	}

	void Rectangle::initIndicesPos()
	{
		indicesPos.count = 6;
	}

	void Rectangle::update()
	{
		shader->setUniform("uColor", &rectColor);
		auto vp = getViewport();
		glm::mat4 projection = glm::ortho(0.0f, (float)vp.w, (float)vp.h, 0.0f);
		shader->setUniform("projection", &projection);
	}

}