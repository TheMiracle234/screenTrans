#include "component/Line.h"
#include "util/util.h"

namespace TM {

	Line::Line(
		Window& window, int x1, int y1, int x2, int y2, uint32_t RGBA,
		std::unique_ptr<Component> _child, const std::shared_ptr<Shader>& shader
	):
		Component(Line::defaultShader(), window, std::move(_child)), pos{ glm::vec2(x1, y1), glm::vec2(x2, y2) },
		color(colorOf(RGBA))
	{
		TM_COMP_INIT;
	}


	Line::~Line() {
		defaultShader(true);
	}

	glm::vec2 Line::getUpLeft() {
		return {
			std::min(pos[0].x, pos[1].x), 
			std::min(pos[0].y, pos[1].y) 
		};
	}

	std::shared_ptr<Shader> Line::defaultShader(bool destruct) {
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

	void Line::initVerticesIndices()
	{
		indicesPos.offset = getData().push(
			{
				pos[0].x, pos[0].y,
				pos[1].x, pos[1].y,
			},
			{
				0, 1,
			}
		);
	}

	void Line::initIndicesPos()
	{
		indicesPos.count = 2;
	}

	void Line::update() {
		shader->setUniform("uColor", &color);
		auto vp = getViewport();
		glm::mat4 projection = glm::ortho(0.0f, (float)vp.w, (float)vp.h, 0.0f);
		shader->setUniform("projection", &projection);
	}

}