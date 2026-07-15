#pragma once

#include "component/Component.h"
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <array>

namespace TM {

	class Triangle : TM_public Component
	{
	TM_private:
		//int getWidth();
		//int getHeight();
		glm::vec2 getUpLeft();

	TM_protected:
		std::array<glm::vec2, 3> pos;
		glm::vec4 color;

	TM_public:
		Triangle(
			const glm::vec2& pos1, const glm::vec2& pos2, const glm::vec2& pos3, 
			Window& window, uint32_t RGBA = 0x4477FFFF, std::unique_ptr<Component> _child = nullptr
		);
		~Triangle();

	// override
	TM_protected:
		void initVerticesIndices() override;
		void initIndicesPos() override;

	TM_public:
		unsigned int getGlDrawMode() const override { return GL_TRIANGLES; }
		glm::vec2 getRelPos() override { return getUpLeft(); }
		void update() override;

	};

}