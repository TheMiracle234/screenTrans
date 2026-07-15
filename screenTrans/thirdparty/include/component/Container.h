#pragma once

#include "component/Component.h"

namespace TM
{
	class Container: public Component
	{
	TM_private:
		glm::vec2 pos;
		float width;
		float height;

	TM_public:
		Container(Window& window, int x, int y, int w, int h, uint32_t bkRGBA = 0x44444444, std::unique_ptr<Component> _child = nullptr);
		virtual ~Container(){}

	// static
	TM_protected:
		void initVerticesIndices() {};
		void initIndicesPos() {};

	TM_public:
		const Viewport getViewport() override ;
		unsigned int getGlDrawMode() const override { return 0; }
		glm::vec2 getRelPos() override { return pos; }
		void update() override {};
	};
}

