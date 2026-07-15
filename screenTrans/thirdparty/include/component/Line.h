#pragma once

#include "component/Component.h"
#include <glm/vec2.hpp>

namespace TM {

	class Line : TM_public Component
	{
	TM_protected:
		glm::vec2 pos[2];
		glm::vec4 color;
		glm::vec2 getUpLeft();

	TM_public:
		Line(
			Window& window, int x1, int y1, int x2, int y2, uint32_t RGBA = 0x444444FF, 
			std::unique_ptr<Component> _child = nullptr, const std::shared_ptr<Shader>& shader = nullptr
		);
		virtual ~Line();

		// static 
	TM_public:
		static std::shared_ptr<Shader> defaultShader(bool destruct = false);

		// override
	TM_protected:
		void initVerticesIndices() override;
		void initIndicesPos() override;

	TM_public:
		unsigned int getGlDrawMode() const override { return GL_LINES; }
		glm::vec2 getRelPos() override { return getUpLeft(); }
		void update() override;
	};

}