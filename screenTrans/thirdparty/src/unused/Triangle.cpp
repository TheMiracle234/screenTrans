#include "component/Triangle.h"
#include "window/Window.h"
#include "component/Rectangle.h"
#include "util/util.h"

#include <algorithm>

namespace TM {
	
	//int Triangle::getWidth()
	//{
	//	float min_x = std::min({ pos[0].x, pos[1].x, pos[2].x });
	//	float max_x = std::max({ pos[0].x, pos[1].x, pos[2].x });
	//	return static_cast<int>(max_x - min_x);
	//}

	//int Triangle::getHeight()
	//{
	//	float min_y = std::min({ pos[0].y, pos[1].y, pos[2].y });
	//	float max_y = std::max({ pos[0].y, pos[1].y, pos[2].y });
	//	return static_cast<int>(max_y - min_y);
	//}

	glm::vec2 Triangle::getUpLeft()
	{
		return { 
			std::min({ pos[0].x, pos[1].x, pos[2].x }) , 
			std::min({ pos[0].y, pos[1].y, pos[2].y }) 
		};
	}

	Triangle::Triangle(
		const glm::vec2& pos1, const glm::vec2& pos2, const glm::vec2& pos3, 
		Window& window, uint32_t RGBA, std::unique_ptr<Component> _child
	):
		Component(Rectangle::defaultShader(), window, std::move(_child)), pos({pos1, pos2, pos3}), color(colorOf(RGBA))
	{
		TM_COMP_INIT;
	}

	Triangle::~Triangle()
	{
		Rectangle::defaultShader(true);
	}

	void Triangle::initVerticesIndices()
	{
		indicesPos.offset = getData().push(
			{
				pos[0].x, pos[0].y,
				pos[1].x, pos[1].y,
				pos[2].x, pos[2].y
			},
			{
				0, 1, 2,
			}
		);
	}

	void Triangle::initIndicesPos()
	{
		indicesPos.count = 3;
	}

	void Triangle::update()
	{
		shader->setUniform("uColor", &color);
		auto vp = getViewport();
		glm::mat4 projection = glm::ortho(0.0f, (float)vp.w, (float)vp.h, 0.0f);
		shader->setUniform("projection", &projection);
	}

}